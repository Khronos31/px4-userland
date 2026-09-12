# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PX4_VERSION_LINE)
string(LENGTH "${PX4_VERSION_LINE}" PX4_VERSION_LENGTH)
if(PX4_VERSION_LENGTH LESS 2)
    message(FATAL_ERROR "VERSION must contain exactly N.N.N followed by one newline")
endif()
math(EXPR PX4_VERSION_LAST_INDEX "${PX4_VERSION_LENGTH} - 1")
string(SUBSTRING "${PX4_VERSION_LINE}" ${PX4_VERSION_LAST_INDEX} 1 PX4_VERSION_TRAILING)
string(SUBSTRING "${PX4_VERSION_LINE}" 0 ${PX4_VERSION_LAST_INDEX} PX4_PROJECT_VERSION)
if(NOT PX4_VERSION_TRAILING STREQUAL "\n" OR
        PX4_PROJECT_VERSION MATCHES "[\r\n]" OR
        NOT PX4_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain exactly N.N.N followed by one newline")
endif()
project(px4_userland VERSION "${PX4_PROJECT_VERSION}" LANGUAGES CXX)

option(PX4_BUILD_TESTS "Build offline portable-core tests" OFF)
option(PX4_ENABLE_LIBUSB "Build the native libusb transport" ON)
option(PX4_BUILD_TOOLS "Build developer-only USB probe tools" OFF)
option(PX4_BUILD_ANDROID_LINKCHECK "Build the Android-only final link check" OFF)
option(PX4_BUILD_PCSC_IFD "Build the POSIX PC/SC IFD Handler when headers are available" ON)
option(PX4_REQUIRE_PCSC_IFD "Fail configuration when the PC/SC IFD Handler cannot be built" OFF)
set(PX4_ANDROID_NDK_ROOT_MAP "" CACHE PATH
    "Android NDK root to remove from compiler debug paths")
set(PX4_ANDROID_LINK_MAP_DIR "" CACHE PATH
    "Optional output directory for Android production executable link maps")

if(ANDROID)
    # Release ELFs must not retain the machine-specific source, build, or NDK
    # roots emitted by Clang's DWARF and macro expansion.  Keep the mapped
    # value relative so an unstripped developer build is reproducible too.
    set(px4_android_prefix_map_flags
        "-fdebug-compilation-dir=."
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-ffile-prefix-map=${CMAKE_BINARY_DIR}=."
        "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=."
        "-fmacro-prefix-map=${CMAKE_BINARY_DIR}=."
    )
    if(PX4_ANDROID_NDK_ROOT_MAP)
        list(APPEND px4_android_prefix_map_flags
            "-ffile-prefix-map=${PX4_ANDROID_NDK_ROOT_MAP}=."
            "-fdebug-prefix-map=${PX4_ANDROID_NDK_ROOT_MAP}=."
            "-fmacro-prefix-map=${PX4_ANDROID_NDK_ROOT_MAP}=."
        )
    endif()
    add_compile_options(${px4_android_prefix_map_flags})

    # Every Android executable/shared-library final link in this project inherits this
    # contract, including future daemons, CLIs, JNI libraries, and developer tools. Every
    # packaged ELF must additionally pass scripts/verify-android-elf.sh before publication.
    # Android builds use the same hardened PIE contract as the production probe.
    add_link_options(
        "-Wl,-z,relro"
        "-Wl,-z,now"
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
        "-Wl,--no-undefined"
    )
endif()

set(PX4_LIBUSB_INCLUDE_DIR "" CACHE PATH "libusb-1.0 include directory override")
set(PX4_LIBUSB_LIBRARY "" CACHE FILEPATH "libusb-1.0 library override")

find_package(Threads REQUIRED)

if(PX4_ENABLE_LIBUSB)
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        # FreeBSD 15.1 supplies libusb 1.0.16 in the base system, while
        # the APIs required by native Q3U4 operation are available.
        set(px4_libusb_pkg_config_spec "libusb-1.0")
        set(px4_libusb_requirement "libusb-1.0")
    else()
        set(px4_libusb_pkg_config_spec "libusb-1.0>=1.0.23")
        set(px4_libusb_requirement "libusb-1.0 >= 1.0.23")
    endif()
    find_package(PkgConfig QUIET)
    if(PX4_LIBUSB_INCLUDE_DIR AND PX4_LIBUSB_LIBRARY)
        add_library(px4_libusb UNKNOWN IMPORTED)
        set_target_properties(px4_libusb PROPERTIES
            IMPORTED_LOCATION "${PX4_LIBUSB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PX4_LIBUSB_INCLUDE_DIR}"
        )
        set(PX4_LIBUSB_TARGET px4_libusb)
    elseif(PkgConfig_FOUND)
        pkg_check_modules(PC_LIBUSB QUIET IMPORTED_TARGET
            "${px4_libusb_pkg_config_spec}")
    endif()

    if(NOT PX4_LIBUSB_TARGET AND TARGET PkgConfig::PC_LIBUSB)
        set(PX4_LIBUSB_TARGET PkgConfig::PC_LIBUSB)
    elseif(NOT PX4_LIBUSB_TARGET)
        if(NOT PX4_LIBUSB_INCLUDE_DIR)
            find_path(PX4_LIBUSB_INCLUDE_DIR
                NAMES libusb.h
                PATH_SUFFIXES libusb-1.0
            )
        endif()
        if(NOT PX4_LIBUSB_LIBRARY)
            find_library(PX4_LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
        endif()
        if(NOT PX4_LIBUSB_INCLUDE_DIR OR NOT PX4_LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "${px4_libusb_requirement} is required; use pkg-config or set "
                "PX4_LIBUSB_INCLUDE_DIR and PX4_LIBUSB_LIBRARY")
        endif()
        add_library(px4_libusb UNKNOWN IMPORTED)
        set_target_properties(px4_libusb PROPERTIES
            IMPORTED_LOCATION "${PX4_LIBUSB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PX4_LIBUSB_INCLUDE_DIR}"
        )
        set(PX4_LIBUSB_TARGET px4_libusb)
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(px4_userland_core STATIC
    userland/src/card.cpp
    userland/src/card_service.cpp
    userland/src/error.cpp
    userland/src/firmware.cpp
    userland/src/identity.cpp
    userland/src/ipc.cpp
    userland/src/it930x.cpp
    userland/src/it930x_protocol.cpp
    userland/src/logging.cpp
    userland/src/bridge_i2c.cpp
    userland/src/r850.cpp
    userland/src/rt710.cpp
    userland/src/tc90522.cpp
    userland/src/q3u4_frontend.cpp
    userland/src/q3u4_card_backend.cpp
    userland/src/q3u4_tuner_backend.cpp
    userland/src/tuner_service.cpp
    userland/src/q3u4_power.cpp
    userland/src/q3u4_lnb_power.cpp
    userland/src/tagged_ts_demux.cpp
)

if(NOT PX4_BUILD_TESTS)
    target_sources(px4_userland_core PRIVATE userland/src/q3u4_stream.cpp)
endif()

if(PX4_BUILD_TESTS)
    # Tests compile a private-access copy so the reader wait barrier cannot
    # become a release symbol.  Tools still use this production copy below.
    add_library(px4_userland_stream_runtime STATIC userland/src/q3u4_stream.cpp)
    target_include_directories(px4_userland_stream_runtime
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
    )
    target_compile_features(px4_userland_stream_runtime PUBLIC cxx_std_17)
    target_link_libraries(px4_userland_stream_runtime PUBLIC px4_userland_core)
endif()

target_include_directories(px4_userland_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
)

target_compile_features(px4_userland_core PUBLIC cxx_std_17)
target_link_libraries(px4_userland_core PUBLIC Threads::Threads)

function(px4_enable_project_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fno-exceptions
        -fno-rtti
    )
endfunction()

px4_enable_project_warnings(px4_userland_core)
if(TARGET px4_userland_stream_runtime)
    px4_enable_project_warnings(px4_userland_stream_runtime)
endif()

if(PX4_ENABLE_LIBUSB)
    add_library(px4_userland_libusb_transport STATIC
        userland/src/libusb_transport.cpp
    )
    target_include_directories(px4_userland_libusb_transport
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
    )
    target_compile_features(px4_userland_libusb_transport PUBLIC cxx_std_17)
    target_link_libraries(px4_userland_libusb_transport PUBLIC
        px4_userland_core
        ${PX4_LIBUSB_TARGET}
    )
    if(ANDROID)
        target_link_libraries(px4_userland_libusb_transport PUBLIC log)
    endif()
    px4_enable_project_warnings(px4_userland_libusb_transport)
endif()

# Filesystem AF_UNIX is a POSIX adapter, including Android API 24.  Keep its
# platform headers out of the portable core and exclude only non-POSIX hosts.
if(UNIX)
    add_library(px4_userland_posix_ipc STATIC
        userland/src/control_client.cpp
        userland/src/control_server.cpp
        userland/src/posix_ipc.cpp
        userland/src/posix_tuner_nonce.cpp
    )
    target_include_directories(px4_userland_posix_ipc
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
    )
    target_compile_features(px4_userland_posix_ipc PUBLIC cxx_std_17)
    target_link_libraries(px4_userland_posix_ipc PUBLIC px4_userland_core)
    px4_enable_project_warnings(px4_userland_posix_ipc)
    if(NOT PX4_BUILD_TESTS)
        target_sources(px4_userland_posix_ipc PRIVATE userland/src/control_workers.cpp)
    else()
        # Keep the failure-injection member out of every production archive.
        # The test executable compiles its private-access copy directly below;
        # this runtime copy exists only to satisfy production-tool links.
        add_library(px4_userland_posix_workers STATIC
            userland/src/control_workers.cpp
        )
        target_include_directories(px4_userland_posix_workers
            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
        )
        target_compile_features(px4_userland_posix_workers PUBLIC cxx_std_17)
        target_link_libraries(px4_userland_posix_workers PUBLIC px4_userland_core)
        px4_enable_project_warnings(px4_userland_posix_workers)
    endif()
endif()

set(PX4_PCSC_IFD_AVAILABLE OFF)
if(PX4_REQUIRE_PCSC_IFD AND NOT PX4_BUILD_PCSC_IFD)
    message(FATAL_ERROR "PX4_REQUIRE_PCSC_IFD requires PX4_BUILD_PCSC_IFD=ON")
endif()
if(PX4_BUILD_PCSC_IFD AND UNIX AND NOT ANDROID)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PX4_PCSC_LITE QUIET libpcsclite)
    endif()
    find_path(PX4_PCSC_IFD_INCLUDE_DIR
        NAMES ifdhandler.h
        HINTS ${PX4_PCSC_LITE_INCLUDE_DIRS}
        PATH_SUFFIXES PCSC PCSC.framework/Headers
    )
    if(PX4_PCSC_IFD_INCLUDE_DIR)
        set(PX4_PCSC_IFD_AVAILABLE ON)
        set_target_properties(px4_userland_core px4_userland_posix_ipc PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
        add_library(px4_userland_pcsc_ifd_core STATIC
            userland/src/pcsc_ifd_adapter.cpp
        )
        target_include_directories(px4_userland_pcsc_ifd_core
            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        )
        target_compile_features(px4_userland_pcsc_ifd_core PUBLIC cxx_std_17)
        target_link_libraries(px4_userland_pcsc_ifd_core PUBLIC px4_userland_posix_ipc)
        set_target_properties(px4_userland_pcsc_ifd_core PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
        px4_enable_project_warnings(px4_userland_pcsc_ifd_core)

        add_library(px4_ifdhandler SHARED userland/src/pcsc_ifd.cpp)
        target_include_directories(px4_ifdhandler PRIVATE
            ${PX4_PCSC_IFD_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        )
        target_compile_features(px4_ifdhandler PRIVATE cxx_std_17)
        target_link_libraries(px4_ifdhandler PRIVATE
            px4_userland_pcsc_ifd_core
        )
        set_target_properties(px4_ifdhandler PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            OUTPUT_NAME px4-userland-ifd
        )
        if(APPLE)
            set(PX4_IFD_BUNDLE_DIR
                "${CMAKE_CURRENT_BINARY_DIR}/px4-userland-ifd.bundle")
            set(PX4_IFD_BUNDLE_EXECUTABLE_NAME
                "${CMAKE_SHARED_LIBRARY_PREFIX}px4-userland-ifd${CMAKE_SHARED_LIBRARY_SUFFIX}")
            set(PX4_IFD_BUNDLE_INFO_PLIST
                "${CMAKE_CURRENT_BINARY_DIR}/px4-userland-ifd.Info.plist")
            configure_file(
                packaging/pcsc/macos/Info.plist.in
                "${PX4_IFD_BUNDLE_INFO_PLIST}"
                @ONLY
            )
            add_custom_command(
                OUTPUT
                    "${PX4_IFD_BUNDLE_DIR}/Contents/Info.plist"
                    "${PX4_IFD_BUNDLE_DIR}/Contents/MacOS/${PX4_IFD_BUNDLE_EXECUTABLE_NAME}"
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${PX4_IFD_BUNDLE_DIR}/Contents/MacOS"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${PX4_IFD_BUNDLE_INFO_PLIST}"
                    "${PX4_IFD_BUNDLE_DIR}/Contents/Info.plist"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:px4_ifdhandler>"
                    "${PX4_IFD_BUNDLE_DIR}/Contents/MacOS/${PX4_IFD_BUNDLE_EXECUTABLE_NAME}"
                DEPENDS px4_ifdhandler "${PX4_IFD_BUNDLE_INFO_PLIST}"
                COMMENT "Creating px4-userland macOS IFD bundle"
                VERBATIM
            )
            add_custom_target(px4_ifd_bundle ALL
                DEPENDS
                    "${PX4_IFD_BUNDLE_DIR}/Contents/Info.plist"
                    "${PX4_IFD_BUNDLE_DIR}/Contents/MacOS/${PX4_IFD_BUNDLE_EXECUTABLE_NAME}"
            )
            set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
                "${PX4_IFD_BUNDLE_DIR}")
            target_link_options(px4_ifdhandler PRIVATE
                "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/scripts/px4-ifd.exports.macos"
            )
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(px4_ifdhandler PRIVATE
                "LINKER:--as-needed"
                "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/scripts/px4-ifd.exports.map"
            )
        endif()
        px4_enable_project_warnings(px4_ifdhandler)
    elseif(PX4_REQUIRE_PCSC_IFD)
        message(FATAL_ERROR
            "PC/SC IFD header ifdhandler.h was not found; install libpcsclite-dev or provide PX4_PCSC_IFD_INCLUDE_DIR")
    else()
        message(STATUS "PC/SC IFD header ifdhandler.h not found; px4_ifdhandler is disabled")
    endif()
elseif(PX4_REQUIRE_PCSC_IFD)
    message(FATAL_ERROR "The PC/SC IFD Handler requires a non-Android POSIX host")
endif()

if(PX4_BUILD_TESTS)
    enable_testing()

    if(TARGET px4_userland_posix_ipc)
        set(PX4_ENABLE_POSIX_IPC_VALUE 1)
    else()
        set(PX4_ENABLE_POSIX_IPC_VALUE 0)
    endif()

    add_library(px4_userland_test_support STATIC
        userland/src/it930x_test_access.cpp
        userland/src/it930x_probe_args.cpp
        userland/src/px4d_args.cpp
        userland/src/frontend_probe_support.cpp
        userland/src/ts_probe_support.cpp
        userland/src/mock_transport.cpp
        userland/src/libusb_transport_test_access.cpp
    )
    target_include_directories(px4_userland_test_support
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
    )
    target_compile_features(px4_userland_test_support PUBLIC cxx_std_17)
    px4_enable_project_warnings(px4_userland_test_support)

    add_executable(px4_userland_tests
        userland/tests/card_tests.cpp
        userland/tests/card_service_tests.cpp
        userland/tests/test_main.cpp
        userland/tests/frontend_tests.cpp
        userland/tests/it930x_card_tests.cpp
        userland/tests/ipc_tests.cpp
        userland/tests/ipc_state_tests.cpp
        userland/tests/px4d_args_tests.cpp
        userland/tests/px4d_signal_tests.cpp
        userland/tests/q3u4_frontend_tests.cpp
        userland/tests/q3u4_card_backend_tests.cpp
        userland/tests/q3u4_power_tests.cpp
        userland/tests/q3u4_lnb_power_tests.cpp
        userland/tests/tuner_service_tests.cpp
        userland/tests/frontend_probe_tests.cpp
        userland/tests/ts_probe_tests.cpp
        userland/tests/r850_tests.cpp
        userland/tests/rt710_tests.cpp
        userland/tests/tagged_ts_demux_tests.cpp
        userland/tests/q3u4_stream_tests.cpp
    )
    if(TARGET px4_userland_posix_ipc)
        target_sources(px4_userland_tests PRIVATE userland/src/control_workers.cpp)
    endif()
    target_link_libraries(px4_userland_tests PRIVATE px4_userland_core px4_userland_test_support)
    if(PX4_ENABLE_LIBUSB)
        target_link_libraries(px4_userland_tests PRIVATE px4_userland_libusb_transport)
    endif()
    target_compile_features(px4_userland_tests PRIVATE cxx_std_17)
    target_compile_definitions(px4_userland_tests PRIVATE
        PX4_ENABLE_LIBUSB=$<BOOL:${PX4_ENABLE_LIBUSB}>
        PX4_ENABLE_POSIX_IPC=${PX4_ENABLE_POSIX_IPC_VALUE}
        PX4_CONTROL_WORKERS_TEST_ACCESS=1
        PX4_CONTROL_SERVER_TEST_ACCESS=1
        PX4_Q3U4_STREAM_TEST_ACCESS=1
    )
    target_include_directories(px4_userland_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/tools
    )
    target_sources(px4_userland_tests PRIVATE userland/src/q3u4_stream.cpp)
    target_sources(px4_userland_tests PRIVATE userland/tools/px4d_signals.cpp)
    px4_enable_project_warnings(px4_userland_tests)

    if(TARGET px4_userland_posix_ipc)
        target_sources(px4_userland_tests PRIVATE
            userland/src/posix_ipc.cpp
            userland/src/control_server.cpp
            userland/tests/control_integration_tests.cpp
            userland/tests/posix_ipc_tests.cpp
            userland/tests/posix_tuner_nonce_tests.cpp
            userland/tests/control_workers_tests.cpp
            userland/tools/px4_ts_core.cpp
            userland/tools/px4_ts_posix.cpp
            userland/tests/px4_ts_tests.cpp
            userland/tools/px4ctl_format.cpp
            userland/tests/px4ctl_format_tests.cpp
        )
        target_link_libraries(px4_userland_tests PRIVATE px4_userland_posix_ipc)
        target_compile_definitions(px4_userland_tests PRIVATE
            PX4_POSIX_IPC_TEST_ACCESS=1
        )
    endif()

    if(PX4_PCSC_IFD_AVAILABLE)
        add_executable(px4_ifd_tests userland/tests/pcsc_ifd_tests.cpp)
        target_include_directories(px4_ifd_tests PRIVATE
            ${PX4_PCSC_IFD_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        )
        target_compile_features(px4_ifd_tests PRIVATE cxx_std_17)
        target_link_libraries(px4_ifd_tests PRIVATE
            px4_ifdhandler
            px4_userland_pcsc_ifd_core
            px4_userland_posix_ipc
        )
        if(TARGET px4_userland_posix_workers)
            target_link_libraries(px4_ifd_tests PRIVATE px4_userland_posix_workers)
        endif()
        px4_enable_project_warnings(px4_ifd_tests)
        add_test(NAME px4_ifd_tests COMMAND px4_ifd_tests)
        add_test(NAME px4_ifd_symbols
            COMMAND ${CMAKE_COMMAND}
                -DNM=${CMAKE_NM}
                -DLIBRARY=$<TARGET_FILE:px4_ifdhandler>
                -DIS_APPLE=${APPLE}
                -DBUNDLE=${PX4_IFD_BUNDLE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-ifd-symbols.cmake
        )
    endif()

    add_test(NAME px4_userland_tests COMMAND px4_userland_tests)
    add_executable(px4_userland_tuner_service_settle_tests
        userland/tests/tuner_service_settle_tests.cpp)
    target_link_libraries(px4_userland_tuner_service_settle_tests
        PRIVATE px4_userland_core)
    target_include_directories(px4_userland_tuner_service_settle_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/include)
    target_compile_features(px4_userland_tuner_service_settle_tests PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4_userland_tuner_service_settle_tests)
    add_test(NAME px4_userland_tuner_service_settle_tests
             COMMAND px4_userland_tuner_service_settle_tests)
    add_executable(px4_userland_q3u4_power_order_tests
        userland/tests/q3u4_power_order_tests.cpp)
    target_link_libraries(px4_userland_q3u4_power_order_tests
        PRIVATE px4_userland_core)
    target_include_directories(px4_userland_q3u4_power_order_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/src)
    target_compile_features(px4_userland_q3u4_power_order_tests PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4_userland_q3u4_power_order_tests)
    add_test(NAME px4_userland_q3u4_power_order_tests
             COMMAND px4_userland_q3u4_power_order_tests)
    if(PX4_ENABLE_LIBUSB)
        add_test(NAME px4_userland_fail_safe_abandonment
            COMMAND px4_userland_tests --fail-safe-abandonment)
        set_tests_properties(px4_userland_fail_safe_abandonment PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

        add_executable(px4_userland_libusb_detach_shutdown_tests
            userland/tests/libusb_detach_shutdown_tests.cpp)
        target_link_libraries(px4_userland_libusb_detach_shutdown_tests
            PRIVATE px4_userland_core px4_userland_test_support
                    px4_userland_libusb_transport)
        target_include_directories(px4_userland_libusb_detach_shutdown_tests
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/src)
        target_compile_features(px4_userland_libusb_detach_shutdown_tests
            PRIVATE cxx_std_17)
        px4_enable_project_warnings(px4_userland_libusb_detach_shutdown_tests)
        add_test(NAME px4_userland_libusb_detach_shutdown_tests
            COMMAND px4_userland_libusb_detach_shutdown_tests)
    endif()
endif()

if(TARGET px4_userland_posix_ipc)
    add_executable(px4ctl
        userland/tools/px4ctl.cpp
        userland/tools/px4ctl_format.cpp
    )
    target_link_libraries(px4ctl PRIVATE px4_userland_posix_ipc)
    target_include_directories(px4ctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/include)
    target_compile_features(px4ctl PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4ctl)

    add_executable(px4-ts
        userland/tools/px4_ts.cpp
        userland/tools/px4_ts_core.cpp
        userland/tools/px4_ts_posix.cpp
    )
    target_link_libraries(px4-ts PRIVATE px4_userland_posix_ipc)
    target_include_directories(px4-ts PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/tools
    )
    target_compile_features(px4-ts PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4-ts)

    if(PX4_ENABLE_LIBUSB)
        add_executable(px4d
            userland/tools/px4d.cpp
            userland/tools/px4d_signals.cpp
            userland/src/px4d_args.cpp
        )
        target_link_libraries(px4d PRIVATE
            px4_userland_posix_ipc
            px4_userland_libusb_transport
        )
        if(TARGET px4_userland_posix_workers)
            target_link_libraries(px4d PRIVATE px4_userland_posix_workers)
        endif()
        if(TARGET px4_userland_stream_runtime)
            target_link_libraries(px4d PRIVATE px4_userland_stream_runtime)
        endif()
        target_include_directories(px4d PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
            ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
        )
        target_compile_features(px4d PRIVATE cxx_std_17)
        px4_enable_project_warnings(px4d)
    endif()
endif()

if(PX4_BUILD_TOOLS)
    if(NOT PX4_ENABLE_LIBUSB)
        message(FATAL_ERROR "PX4_BUILD_TOOLS requires PX4_ENABLE_LIBUSB=ON")
    endif()
    add_executable(px4-usb-probe userland/tools/usb_probe.cpp)
    target_link_libraries(px4-usb-probe PRIVATE px4_userland_libusb_transport)
    target_include_directories(px4-usb-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/include)
    target_compile_features(px4-usb-probe PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4-usb-probe)

    add_executable(px4-it930x-probe
        userland/tools/it930x_probe.cpp
        userland/src/it930x_probe_args.cpp
    )
    target_link_libraries(px4-it930x-probe PRIVATE px4_userland_libusb_transport)
    target_include_directories(px4-it930x-probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
    )
    target_compile_features(px4-it930x-probe PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4-it930x-probe)

    add_executable(px4-frontend-probe
        userland/tools/frontend_probe.cpp
        userland/src/frontend_probe_support.cpp
    )
    target_link_libraries(px4-frontend-probe PRIVATE px4_userland_libusb_transport)
    target_include_directories(px4-frontend-probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
    )
    target_compile_features(px4-frontend-probe PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4-frontend-probe)

    add_executable(px4-ts-probe
        userland/tools/ts_probe.cpp
        userland/src/frontend_probe_support.cpp
        userland/src/ts_probe_support.cpp
    )
    target_link_libraries(px4-ts-probe PRIVATE px4_userland_libusb_transport)
    target_include_directories(px4-ts-probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
    )
    target_compile_features(px4-ts-probe PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4-ts-probe)

    add_executable(px4-card-probe
        userland/tools/card_probe.cpp
        userland/src/frontend_probe_support.cpp
    )
    target_link_libraries(px4-card-probe PRIVATE px4_userland_libusb_transport)
    target_include_directories(px4-card-probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/include
        ${CMAKE_CURRENT_SOURCE_DIR}/userland/src
    )
    target_compile_features(px4-card-probe PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4-card-probe)
endif()

if(PX4_BUILD_ANDROID_LINKCHECK)
    if(NOT ANDROID)
        message(FATAL_ERROR "PX4_BUILD_ANDROID_LINKCHECK requires an Android toolchain")
    endif()
    if(NOT PX4_ENABLE_LIBUSB)
        message(FATAL_ERROR "PX4_BUILD_ANDROID_LINKCHECK requires PX4_ENABLE_LIBUSB=ON")
    endif()
    add_executable(px4-android-linkcheck userland/tools/android_linkcheck.cpp)
    target_link_libraries(px4-android-linkcheck PRIVATE px4_userland_libusb_transport)
    target_link_libraries(px4-android-linkcheck PRIVATE log c)
    target_include_directories(px4-android-linkcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/userland/include)
    target_compile_features(px4-android-linkcheck PRIVATE cxx_std_17)
    px4_enable_project_warnings(px4-android-linkcheck)
    target_link_options(px4-android-linkcheck PRIVATE
        -Wl,--no-as-needed
        -Wl,--no-gc-sections
        -Wl,--undefined=libusb_init
        -Wl,--undefined=libusb_wrap_sys_device
        -Wl,--undefined=libusb_close
        -Wl,--undefined=__android_log_print
        -Wl,--undefined=memcpy
    )
endif()

if(ANDROID AND PX4_ANDROID_LINK_MAP_DIR)
    file(MAKE_DIRECTORY "${PX4_ANDROID_LINK_MAP_DIR}")
    foreach(target px4d px4-ts px4ctl)
        if(TARGET ${target})
            target_link_options(${target} PRIVATE
                "LINKER:-Map,${PX4_ANDROID_LINK_MAP_DIR}/${target}.map")
        endif()
    endforeach()
endif()
