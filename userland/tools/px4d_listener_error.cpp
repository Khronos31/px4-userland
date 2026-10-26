// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_listener_error.h"

#include "px4/error.h"

namespace px4::userland::tools {
namespace {

using dtv::userland::host_listener_context::ListenerDiagnosticReason;
using dtv::userland::host_listener_context::RuntimeDirectoryLayer;
using dtv::userland::host_runtime::EndpointDiagnosticReason;

const char* directory_layer_name(RuntimeDirectoryLayer layer)
{
    switch (layer) {
    case RuntimeDirectoryLayer::runtime_root:
        return "root";
    case RuntimeDirectoryLayer::product:
        return "product";
    case RuntimeDirectoryLayer::version:
        return "version";
    case RuntimeDirectoryLayer::instance:
        return "instance";
    }
    return "runtime";
}

const char* endpoint_reason(const dtv::userland::host_runtime::EndpointDiagnostic& endpoint,
                            std::string& advice)
{
    switch (endpoint.reason) {
    case EndpointDiagnosticReason::runtime_directory:
        advice = "Check --runtime-dir PATH; it must be an absolute path.";
        return "runtime directory value is invalid";
    case EndpointDiagnosticReason::product:
        return "product endpoint component is invalid";
    case EndpointDiagnosticReason::instance:
        advice = "Use a valid --instance TOKEN value shared by px4d and its clients.";
        return "instance endpoint component is invalid";
    case EndpointDiagnosticReason::endpoint_name:
        return "endpoint name is invalid";
    case EndpointDiagnosticReason::path_too_long: {
        advice = "Shorten --runtime-dir or use a shorter --instance TOKEN shared by "
                 "px4d and its clients.";
        return nullptr;
    }
    case EndpointDiagnosticReason::none:
        return nullptr;
    }
    return nullptr;
}

}  // namespace

std::string format_listener_error(
    dtv::userland::Error error,
    const dtv::userland::host_listener_context::ListenerDiagnostic& diagnostic,
    const char* operation)
{
    std::string message = "px4d: endpoint ";
    message += operation;
    message += " failed: ";
    message += error_string(static_cast<Error>(error));

    const std::string directory = std::string(directory_layer_name(diagnostic.directory_layer)) +
                                  " runtime directory";
    std::string advice;
    switch (diagnostic.reason) {
    case ListenerDiagnosticReason::endpoint_path: {
        const char* reason = endpoint_reason(diagnostic.endpoint, advice);
        if (diagnostic.endpoint.reason == EndpointDiagnosticReason::path_too_long) {
            message += ": ";
            if (!diagnostic.endpoint_name.empty()) {
                message.append(diagnostic.endpoint_name.data(), diagnostic.endpoint_name.size());
            }
            message += " path is too long for this build (";
            message += std::to_string(diagnostic.endpoint.path_bytes);
            message += " pathname bytes + NUL require ";
            message += std::to_string(diagnostic.endpoint.required_bytes);
            message += " bytes; native field capacity is ";
            message += std::to_string(diagnostic.endpoint.native_capacity);
            message += " bytes including the terminating NUL).";
        } else if (reason != nullptr) {
            message += ": ";
            message += reason;
            message += ".";
        }
        break;
    }
    case ListenerDiagnosticReason::runtime_directory_missing:
        message += ": the " + directory + " does not exist. Create the path selected by "
                   "--runtime-dir PATH.";
        break;
    case ListenerDiagnosticReason::runtime_directory_lookup:
        message += ": the " + directory + " could not be inspected. Check --runtime-dir PATH "
                   "and its parent permissions.";
        break;
    case ListenerDiagnosticReason::runtime_directory_type:
        message += ": the " + directory + " must be a real directory, not a symlink. "
                   "Check --runtime-dir PATH.";
        break;
    case ListenerDiagnosticReason::runtime_directory_owner:
        message += ": the " + directory + " must be owned by the current user. "
                   "Check --runtime-dir PATH ownership.";
        break;
    case ListenerDiagnosticReason::runtime_directory_mode:
        message += ": the " + directory + " mode does not match its access setting. Private "
                   "mode requires no group/other permissions; --group requires group "
                   "read/write/execute and no other permissions. Check --runtime-dir PATH.";
        break;
    case ListenerDiagnosticReason::runtime_directory_security:
        message += ": the " + directory + " failed the current-user owner/DACL security "
                   "check. Use a runtime directory owned by the current user with the "
                   "required private DACL.";
        break;
    case ListenerDiagnosticReason::runtime_directory_encoding:
        message += ": --runtime-dir PATH is not valid UTF-8 on Windows; provide a valid "
                   "UTF-8 path.";
        break;
    case ListenerDiagnosticReason::none:
        break;
    }
    if (!advice.empty()) {
        message += " ";
        message += advice;
    }
    return message;
}

std::string format_usb_selection_error(dtv::userland::Error error)
{
    std::string message = "px4d: device selection failed: ";
    message += error_string(static_cast<Error>(error));
    if (error == dtv::userland::Error::INVALID_ARGUMENT) {
        message += ": the requested USB path did not resolve to exactly one ready PX4 "
                   "enclosure. ";
        message += "Use one current USB path for a single-device model or both paths from the "
                   "same paired enclosure; inspect px4d --list-json for the current path(s).";
    }
    return message;
}

std::string format_firmware_error(dtv::userland::Error error)
{
    std::string message = "px4d: firmware: ";
    message += error_string(static_cast<Error>(error));
    if (error == dtv::userland::Error::NOT_FOUND) {
        message += ": the file selected by --firmware PATH could not be opened. "
                   "Check that the path exists and is readable.";
    } else if (error == dtv::userland::Error::FIRMWARE_REJECTED) {
        message += ": the file selected by --firmware PATH failed the supported "
                   "firmware size, hash, or format checks.";
    } else if (error == dtv::userland::Error::INVALID_ARGUMENT) {
        message += ": --firmware PATH must be valid UTF-8 on Windows; provide a valid "
                   "UTF-8 path.";
    }
    return message;
}

}  // namespace px4::userland::tools
