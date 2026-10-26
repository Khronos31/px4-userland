// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_listener_error.h"

#include <cstdio>
#include <string>

namespace {

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "px4d listener error CHECK failed at %s:%d: %s\n",        \
                         __FILE__, __LINE__, #condition);                                   \
            return false;                                                                   \
        }                                                                                   \
    } while (false)

using namespace dtv::userland;
using namespace dtv::userland::host_listener_context;

bool test_runtime_directory_layer_diagnostic()
{
    ListenerDiagnostic diagnostic{};
    diagnostic.reason = ListenerDiagnosticReason::runtime_directory_missing;
    diagnostic.directory_layer = RuntimeDirectoryLayer::instance;
    const std::string message = px4::userland::tools::format_listener_error(
        Error::NOT_FOUND, diagnostic, "reservation");
    CHECK(message.find("instance runtime directory does not exist") != std::string::npos);
    return true;
}

bool test_runtime_directory_encoding_diagnostic()
{
    ListenerDiagnostic diagnostic{};
    diagnostic.reason = ListenerDiagnosticReason::runtime_directory_encoding;
    const std::string message = px4::userland::tools::format_listener_error(
        Error::INVALID_ARGUMENT, diagnostic, "reservation");
    CHECK(message.find("not valid UTF-8 on Windows") != std::string::npos);
    return true;
}

bool test_endpoint_path_diagnostic()
{
    std::string runtime_directory;
#if defined(_WIN32)
    runtime_directory = "C:\\";
#else
    runtime_directory = "/";
#endif
    runtime_directory.append(200U, 'r');
    const host_runtime::EndpointConfig config{
        runtime_directory, "px4-userland", "test", host_runtime::EndpointAccess::private_user};
    host_runtime::EndpointDiagnostic endpoint{};
    const auto path = host_runtime::endpoint_path(config, &endpoint);
    CHECK(!path && path.error() == Error::INVALID_ARGUMENT);
    CHECK(endpoint.reason == host_runtime::EndpointDiagnosticReason::path_too_long);

    ListenerDiagnostic diagnostic{};
    diagnostic.reason = ListenerDiagnosticReason::endpoint_path;
    diagnostic.endpoint = endpoint;
    diagnostic.endpoint_name = "control.sock";

    const std::string message = px4::userland::tools::format_listener_error(
        path.error(), diagnostic, "reservation");
    CHECK(message.find("INVALID_ARGUMENT") != std::string::npos);
    CHECK(message.find("control.sock") != std::string::npos);
    CHECK(message.find(std::to_string(endpoint.path_bytes) + " pathname bytes + NUL require " +
                       std::to_string(endpoint.required_bytes) + " bytes") != std::string::npos);
    CHECK(message.find(std::to_string(endpoint.native_capacity) +
                       " bytes including the terminating NUL") != std::string::npos);
    CHECK(message.find("Shorten --runtime-dir") != std::string::npos);
    CHECK(message.find("px4-userland/v1") == std::string::npos);
    return true;
}

bool test_runtime_directory_security_reason()
{
    ListenerDiagnostic diagnostic{};
    diagnostic.reason = ListenerDiagnosticReason::runtime_directory_owner;
    const std::string message = px4::userland::tools::format_listener_error(
        Error::UNSUPPORTED, diagnostic, "reservation");
    CHECK(message.find("UNSUPPORTED") != std::string::npos);
    CHECK(message.find("owned by the current user") != std::string::npos);
    CHECK(message.find("--runtime-dir") != std::string::npos);
    CHECK(message.find("SID") == std::string::npos);
    return true;
}

bool test_usb_selection_error()
{
    const std::string message = px4::userland::tools::format_usb_selection_error(
        Error::INVALID_ARGUMENT);
    CHECK(message.find("INVALID_ARGUMENT") != std::string::npos);
    CHECK(message.find("exactly one ready PX4 enclosure") != std::string::npos);
    CHECK(message.find("both paths from the same paired enclosure") != std::string::npos);
    CHECK(message.find("px4d --list-json") != std::string::npos);
    CHECK(message.find("current path(s)") != std::string::npos);
    CHECK(message.find("--instance") == std::string::npos);
    return true;
}

bool test_firmware_errors()
{
    const std::string missing = px4::userland::tools::format_firmware_error(Error::NOT_FOUND);
    CHECK(missing.find("NOT_FOUND") != std::string::npos);
    CHECK(missing.find("--firmware PATH") != std::string::npos);
    CHECK(missing.find("exists and is readable") != std::string::npos);

    const std::string rejected = px4::userland::tools::format_firmware_error(
        Error::FIRMWARE_REJECTED);
    CHECK(rejected.find("FIRMWARE_REJECTED") != std::string::npos);
    CHECK(rejected.find("size, hash, or format checks") != std::string::npos);

    const std::string invalid = px4::userland::tools::format_firmware_error(
        Error::INVALID_ARGUMENT);
    CHECK(invalid.find("INVALID_ARGUMENT") != std::string::npos);
    CHECK(invalid.find("valid UTF-8 on Windows") != std::string::npos);
    CHECK(invalid.find("C:\\secret") == std::string::npos);
    return true;
}

}  // namespace

bool run_px4d_listener_error_tests()
{
    return test_endpoint_path_diagnostic() && test_runtime_directory_security_reason() &&
           test_runtime_directory_layer_diagnostic() &&
           test_runtime_directory_encoding_diagnostic() &&
           test_usb_selection_error() && test_firmware_errors();
}
