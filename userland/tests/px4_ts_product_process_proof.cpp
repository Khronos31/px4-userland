// SPDX-License-Identifier: GPL-2.0-only
#define main px4_runtime_v1_server_tests_main
#include "runtime_v1_server_tests.cpp"
#undef main

#include <sys/wait.h>

#ifndef PX4_TS_EXECUTABLE
#error PX4_TS_EXECUTABLE is required
#endif

namespace {

std::string px4_serial_auto_instance()
{
    const DeviceObservation first = bridge(11U, 11U);
    const DeviceObservation second = bridge(12U, 12U);
    const std::array<const DeviceObservation*, 2U> functions{&first, &second};
    const auto resolved = automatic_px4_identity(DeviceModel::px_q3u4,
                                                  "00001205000960", functions, 2U, true);
    if (!resolved) return {};
    const auto instance = identity::auto_instance(resolved.value());
    if (!instance) return {};
    return std::string(instance.value().bytes.data(), instance.value().length);
}

bool run_client(const char* system, const char* channel, std::uint8_t receiver,
                std::uint8_t tag, bool unlimited, const char* selector,
                bool include_instance, bool serial_auto_instance = false)
{
    constexpr std::size_t packet_count = 32U;
    TaggedFakeTransport first;
    TaggedFakeTransport second;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        first, second, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    if (!plane) return false;
    const std::string instance = serial_auto_instance
        ? px4_serial_auto_instance()
        : (unlimited ? "product-cli-process-unlimited" : "product-cli-process-finite");
    if (instance.empty()) return false;
    Fixture f(instance.c_str(), plane.value().get());
    if (!f.start(false, 0x10U, f.instance, true)) return false;
    first.push(tagged_packets(tag, packet_count + 1U));
    const std::string path = f.runtime.path() + "/product.ts";
    std::vector<std::string> arguments{
        PX4_TS_EXECUTABLE, "--device", selector, "--runtime-dir",
        f.runtime.path()};
    if (include_instance) {
        arguments.push_back("--instance");
        arguments.push_back(f.instance);
    }
    arguments.insert(arguments.end(), {"--receiver", std::to_string(receiver), "-c",
                                       channel, "-s", system, "--output", path,
                                       "--time", unlimited ? "-" : "1"});
    std::vector<std::string> complete_arguments = arguments;
    if (std::string_view(system) == "isdb-s") {
        complete_arguments.insert(complete_arguments.end() - 4,
                                  {"--slot", "2"});
    }
    std::vector<char*> argv;
    argv.reserve(complete_arguments.size() + 1U);
    for (const auto& value : complete_arguments)
        argv.push_back(const_cast<char*>(value.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child == 0) {
        ::execv(PX4_TS_EXECUTABLE, argv.data());
        std::_Exit(127);
    }
    if (child < 0) return false;

    bool done = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) {
            done = true;
            break;
        }
        if (waited < 0) break;
        if (unlimited) {
            const auto written = read_file(path);
            if (written.size() >= 188U) {
                (void)::kill(child, SIGINT);
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!done) {
        const auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < stop_deadline) {
            const pid_t waited = ::waitpid(child, &status, WNOHANG);
            if (waited == child) {
                done = true;
                break;
            }
            if (waited < 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (!done) {
        (void)::kill(child, SIGKILL);
        (void)::waitpid(child, &status, 0);
        return false;
    }
    const auto written = read_file(path);
    const std::uint64_t expected_hz = std::string_view(system) == "isdb-t"
        ? 557142857U : 1318000000U;
    const bool aligned = written.size() >= 188U && written.size() % 188U == 0U &&
                         written[0U] == 0x47U;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && aligned &&
           f.backend.last_hz[receiver].load() == expected_hz &&
           f.backend.close_calls[receiver].load() > 0U && f.server_ok.load();
}

bool run_mismatched_device_assertion()
{
    TaggedFakeTransport first;
    TaggedFakeTransport second;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        first, second, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    if (!plane) return false;
    Fixture f("product-cli-process-mismatch", plane.value().get());
    if (!f.start(false, 0x10U, f.instance, true)) return false;
    const std::string path = f.runtime.path() + "/mismatch.ts";
    const std::vector<std::string> arguments{
        PX4_TS_EXECUTABLE, "--device", "serial:00001205000961", "--instance", f.instance,
        "--runtime-dir", f.runtime.path(), "--receiver", "2", "-c", "T27", "-s",
        "isdb-t", "--output", path, "--time", "1"};
    std::vector<char*> argv;
    for (const auto& value : arguments) argv.push_back(const_cast<char*>(value.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child == 0) {
        ::execv(PX4_TS_EXECUTABLE, argv.data());
        std::_Exit(127);
    }
    if (child < 0) return false;
    int status = 0;
    if (::waitpid(child, &status, 0) != child) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 3 && f.lease_ids.calls() == 0U &&
           read_file(path).empty() && f.server_ok.load();
}

bool add_hello_stall_endpoint(const std::string& version_directory,
                              const std::string& instance,
                              std::vector<int>& listeners)
{
    const std::string directory = version_directory + "/" + instance;
    if (!std::filesystem::create_directory(directory) || ::chmod(directory.c_str(), 0700) != 0)
        return false;
    const std::string socket_path = directory + "/control.sock";
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(address.sun_path)) {
        ::close(fd);
        return false;
    }
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1U);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::chmod(socket_path.c_str(), 0600) != 0 || ::listen(fd, 8) != 0) {
        ::close(fd);
        return false;
    }
    listeners.push_back(fd);
    return true;
}

[[maybe_unused]] bool run_aggregate_probe_timeout()
{
    TaggedFakeTransport first;
    TaggedFakeTransport second;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        first, second, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    if (!plane) return false;
    const std::string auto_instance = px4_serial_auto_instance();
    if (auto_instance.empty()) return false;
    Fixture f(auto_instance.c_str(), plane.value().get());
    if (!f.start(false, 0x10U, f.instance, true)) return false;
    const std::string version_directory =
        f.runtime.path() + "/px4-userland/v1";
    std::vector<int> listeners;
    listeners.reserve(255U);
    for (std::size_t index = 0U; index < 255U; ++index) {
        char name[24]{};
        std::snprintf(name, sizeof(name), "stall-%03zu", index);
        if (!add_hello_stall_endpoint(version_directory, name, listeners)) return false;
    }
    const auto candidates = discover_runtime_instances(
        f.runtime.path().c_str(), EndpointAccess::private_user);
    if (!candidates || candidates.value().count != 256U) return false;

    const std::string output_path = f.runtime.path() + "/timeout.ts";
    const std::vector<std::string> arguments{
        PX4_TS_EXECUTABLE, "--device", "serial:00001205000960", "--runtime-dir",
        f.runtime.path(), "--receiver", "2", "-c", "T27", "-s", "isdb-t",
        "--output", output_path, "--time", "1"};
    std::vector<char*> argv;
    for (const auto& value : arguments) argv.push_back(const_cast<char*>(value.c_str()));
    argv.push_back(nullptr);
    const auto started = std::chrono::steady_clock::now();
    const pid_t child = ::fork();
    if (child == 0) {
        ::execv(PX4_TS_EXECUTABLE, argv.data());
        std::_Exit(127);
    }
    if (child < 0) return false;
    int status = 0;
    if (::waitpid(child, &status, 0) != child) return false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    for (const int fd : listeners) ::close(fd);
    return WIFEXITED(status) && WEXITSTATUS(status) == 5 && elapsed >= 1500 &&
           elapsed < 3500 && f.lease_ids.calls() == 0U &&
           read_file(output_path).empty() && f.server_ok.load();
}

[[maybe_unused]] bool run_multiple_matching_endpoints()
{
    TaggedFakeTransport first;
    TaggedFakeTransport second;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        first, second, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    if (!plane) return false;
    const std::string auto_instance = px4_serial_auto_instance();
    if (auto_instance.empty()) return false;
    Fixture f(auto_instance.c_str(), plane.value().get());
    if (!f.start(false, 0x10U, f.instance, true)) return false;

    ControlContext second_context = *f.context;
    constexpr std::string_view second_instance = "second-matching-endpoint";
    second_context.instance.present = true;
    second_context.instance.value.length =
        static_cast<std::uint16_t>(second_instance.size());
    std::memcpy(second_context.instance.value.bytes.data(), second_instance.data(),
                second_instance.size());
    const EndpointConfig second_endpoint{f.runtime.path().c_str(),
        second_instance.data(), kControlEndpointName, EndpointAccess::private_user};
    auto second_server_result = PosixControlServer::create(
        second_endpoint, *f.card, *f.tuner, second_context);
    if (!second_server_result) return false;
    auto second_server = std::move(second_server_result.value());
    std::atomic<bool> stop{false};
    std::atomic<bool> server_ok{true};
    std::thread poller([&]() {
        while (!stop.load()) {
            if (!second_server->poll_once(Timeout{10U})) {
                server_ok.store(false);
                return;
            }
        }
    });

    const std::string output_path = f.runtime.path() + "/ambiguous.ts";
    const std::vector<std::string> arguments{
        PX4_TS_EXECUTABLE, "--device", "serial:00001205000960", "--runtime-dir",
        f.runtime.path(), "--receiver", "2", "-c", "T27", "-s", "isdb-t",
        "--output", output_path, "--time", "1"};
    std::vector<char*> argv;
    for (const auto& value : arguments) argv.push_back(const_cast<char*>(value.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child == 0) {
        ::execv(PX4_TS_EXECUTABLE, argv.data());
        std::_Exit(127);
    }
    int status = 0;
    const bool waited = child > 0 && ::waitpid(child, &status, 0) == child;
    stop.store(true);
    if (poller.joinable()) poller.join();
    (void)second_server->shutdown();
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 3 &&
           f.lease_ids.calls() == 0U && read_file(output_path).empty() &&
           server_ok.load() && f.server_ok.load();
}

}  // namespace

int main()
{
    const bool terrestrial = run_client("isdb-t", "T27", 2U, 3U, false,
                                        "serial:00001205000960", true);
    const bool satellite = run_client("isdb-s", "BS15", 0U, 1U, true,
                                      "serial:00001205000960", true);
    const bool serial_auto_discovery = run_client("isdb-t", "T27", 2U, 3U, false,
                                                   "serial:00001205000960", false, true);
    const bool usb_one_path = run_client("isdb-t", "T27", 2U, 3U, false,
                                         "usb:3-1.1", false, true);
    const bool mismatch = run_mismatched_device_assertion();
    return terrestrial && satellite && serial_auto_discovery && usb_one_path && mismatch ? 0 : 1;
}
