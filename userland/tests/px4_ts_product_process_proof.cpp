// SPDX-License-Identifier: GPL-2.0-only
#define main px4_runtime_v1_server_tests_main
#include "runtime_v1_server_tests.cpp"
#undef main

#include <sys/wait.h>

#ifndef PX4_TS_EXECUTABLE
#error PX4_TS_EXECUTABLE is required
#endif

namespace {

bool run_client(const char* system, const char* channel, std::uint8_t receiver,
                std::uint8_t tag, bool unlimited)
{
    constexpr std::size_t packet_count = 32U;
    TaggedFakeTransport first;
    TaggedFakeTransport second;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        first, second, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    if (!plane) return false;
    Fixture f(unlimited ? "product-cli-process-unlimited" : "product-cli-process-finite",
              plane.value().get());
    if (!f.start()) return false;
    first.push(tagged_packets(tag, packet_count + 1U));
    const std::string path = f.runtime.path() + "/product.ts";
    const std::vector<std::string> arguments{
        PX4_TS_EXECUTABLE, "--instance", f.instance, "--runtime-dir", f.runtime.path(),
        "--receiver", std::to_string(receiver), "-c", channel, "-s", system,
        "--output", path, "--time", unlimited ? "-" : "1"};
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

}  // namespace

int main()
{
    const bool terrestrial = run_client("isdb-t", "T27", 2U, 3U, false);
    const bool satellite = run_client("isdb-s", "BS15", 0U, 1U, true);
    return terrestrial && satellite ? 0 : 1;
}
