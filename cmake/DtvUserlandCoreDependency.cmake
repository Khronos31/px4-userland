# SPDX-License-Identifier: GPL-2.0-only
#
# Locate the immutable MIT dtv-userland-core foundation library.
#
# The dependency is pinned by a full 40-hex commit recorded in
# cmake/dtv-userland-core.revision.  A corresponding-source archive may bundle
# the tree under third_party/dtv-userland-core.  For local or modified builds,
# set FETCHCONTENT_SOURCE_DIR_DTV_USERLAND_CORE (or DTV_USERLAND_CORE_SOURCE_DIR)
# to an explicit source tree; such a build is reported as unpinned and never
# silently falls back to a duplicated implementation.

include_guard(GLOBAL)

include(FetchContent)

set(DTV_USERLAND_CORE_REPOSITORY
    "git@github.com:Khronos31/dtv-userland-core.git"
    CACHE STRING "Git repository for the dtv-userland-core dependency")
set(DTV_USERLAND_CORE_SOURCE_DIR "" CACHE PATH
    "Explicit local dtv-userland-core source tree (local/modified override)")

function(px4_dtv_userland_core_revision out_var)
    set(_file "${CMAKE_CURRENT_SOURCE_DIR}/cmake/dtv-userland-core.revision")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR
            "missing dtv-userland-core lock file: ${_file}")
    endif()
    file(READ "${_file}" _contents)
    string(STRIP "${_contents}" _revision)
    if(_revision MATCHES "[\r\n]" OR NOT _revision MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR
            "${_file} must contain exactly one full 40-hex commit")
    endif()
    string(TOLOWER "${_revision}" _revision)
    string(LENGTH "${_revision}" _length)
    if(NOT _length EQUAL 40 OR NOT _revision MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR
            "${_file} must contain a full 40-hex commit, got '${_revision}'")
    endif()
    if(_revision STREQUAL "0000000000000000000000000000000000000000")
        message(FATAL_ERROR
            "${_file} is not yet pinned; replace the placeholder with the "
            "verified common commit before a non-override build")
    endif()
    set(${out_var} "${_revision}" PARENT_SCOPE)
endfunction()

function(px4_add_dtv_userland_core)
    set(_bundled "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dtv-userland-core")

    if(DTV_USERLAND_CORE_SOURCE_DIR)
        set(_override "${DTV_USERLAND_CORE_SOURCE_DIR}")
    elseif(FETCHCONTENT_SOURCE_DIR_DTV_USERLAND_CORE)
        set(_override "${FETCHCONTENT_SOURCE_DIR_DTV_USERLAND_CORE}")
    else()
        set(_override "")
    endif()

    if(_override)
        if(NOT EXISTS "${_override}/CMakeLists.txt")
            message(FATAL_ERROR
                "dtv-userland-core source override has no CMakeLists.txt: ${_override}")
        endif()
        message(WARNING
            "dtv-userland-core: using explicit local source ${_override}; "
            "this build is not pinned to cmake/dtv-userland-core.revision")
        FetchContent_Declare(dtv_userland_core SOURCE_DIR "${_override}")
    elseif(EXISTS "${_bundled}/CMakeLists.txt")
        px4_dtv_userland_core_revision(_revision)
        message(STATUS
            "dtv-userland-core: using bundled corresponding source ${_bundled}")
        FetchContent_Declare(dtv_userland_core SOURCE_DIR "${_bundled}")
    else()
        px4_dtv_userland_core_revision(_revision)
        message(STATUS
            "dtv-userland-core: fetching pinned commit ${_revision}")
        FetchContent_Declare(dtv_userland_core
            GIT_REPOSITORY "${DTV_USERLAND_CORE_REPOSITORY}"
            GIT_TAG "${_revision}")
    endif()

    # The foundation's own tests must never turn on as product tests.
    set(_saved_build_testing "${BUILD_TESTING}")
    set(BUILD_TESTING OFF)
    # TunerService is a facade over the shared receiver lifecycle engine, and
    # the Q3U4 data plane stores its packet session in the shared capture
    # component.
    set(DTV_USERLAND_BUILD_RECEIVER ON)
    # CardService is a facade over the shared card ownership engine.
    set(DTV_USERLAND_BUILD_CARD ON)
    set(DTV_USERLAND_BUILD_CAPTURE ON)
    set(DTV_USERLAND_BUILD_HOST_CTL ON CACHE BOOL
        "Build the optional GPL host control frontend" FORCE)
    set(DTV_USERLAND_HOST_CTL_FACADE_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/userland/include" CACHE STRING
        "Private host facade include directories required by the GPL host control frontend" FORCE)
    if(UNIX OR WIN32)
        set(DTV_USERLAND_BUILD_HOST_RUNTIME ON CACHE BOOL
            "Build the optional GPL host runtime adapter" FORCE)
        set(DTV_USERLAND_HOST_RUNTIME_PRODUCT "px4-userland" CACHE STRING
            "Product runtime namespace component required by the GPL host runtime adapter" FORCE)
        set(DTV_USERLAND_HOST_RUNTIME_INSTANCE_POLICY "lowercase" CACHE STRING
            "Instance-name policy for the GPL host runtime adapter" FORCE)
        set(DTV_USERLAND_HOST_RUNTIME_FACADE_INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/userland/include" CACHE STRING
            "Private product facade include directories required by the GPL host runtime adapter" FORCE)
    endif()
    if(WIN32 AND PX4_BUILD_WINSCARD)
        # The shared WinSCard adapter is built from one GPL source with the
        # product's explicit identity.  Never let a missing value fall back to
        # another product's defaults.
        set(DTV_USERLAND_BUILD_WINDOWS_WINSCARD ON CACHE BOOL
            "Build the optional GPL-2.0-only Windows WinSCard adapter" FORCE)
        set(DTV_WINDOWS_WINSCARD_NAMESPACE_COMPONENT "px4-userland" CACHE STRING
            "Product runtime namespace component required by the WinSCard adapter" FORCE)
        set(DTV_WINDOWS_WINSCARD_VERSION_COMPONENT "v1" CACHE STRING
            "Product runtime version component required by the WinSCard adapter" FORCE)
        set(DTV_WINDOWS_WINSCARD_PRODUCT_SLUG "px4-userland" CACHE STRING
            "Expected UDTV LIST producer.product required by the WinSCard adapter" FORCE)
        set(DTV_WINDOWS_WINSCARD_READER_PREFIX "PX4 userland (" CACHE STRING
            "Reader display prefix shared by the WinSCard client and proxy" FORCE)
        set(DTV_WINDOWS_WINSCARD_RUNTIME_ENV "PX4_WINSCARD_RUNTIME_DIR" CACHE STRING
            "Runtime-root override environment variable for the WinSCard proxy" FORCE)
        set(DTV_WINDOWS_WINSCARD_HOST_INCLUDE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/userland/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/userland/src" CACHE STRING
            "Private product host facade include directories required by the WinSCard adapter" FORCE)
    else()
        set(DTV_USERLAND_BUILD_WINDOWS_WINSCARD OFF CACHE BOOL
            "Build the optional GPL-2.0-only Windows WinSCard adapter" FORCE)
    endif()
    FetchContent_MakeAvailable(dtv_userland_core)
    set(BUILD_TESTING "${_saved_build_testing}")

    if(NOT TARGET dtv::core)
        message(FATAL_ERROR "dtv-userland-core did not define the dtv::core target")
    endif()
    if(NOT TARGET dtv::receiver)
        message(FATAL_ERROR
            "dtv-userland-core did not define the dtv::receiver target; "
            "the pinned commit predates the receiver lifecycle engine")
    endif()
    if(NOT TARGET dtv::card)
        message(FATAL_ERROR
            "dtv-userland-core did not define the dtv::card target; "
            "the pinned commit predates the card ownership engine")
    endif()
    if(NOT TARGET dtv::capture)
        message(FATAL_ERROR
            "dtv-userland-core did not define the dtv::capture target; "
            "the pinned commit predates the packet-session storage")
    endif()
    if(NOT TARGET dtv::host_ctl)
        message(FATAL_ERROR
            "dtv-userland-core did not define the optional dtv::host_ctl target")
    endif()
    if(UNIX OR WIN32)
        if(NOT TARGET dtv::host_runtime)
            message(FATAL_ERROR
                "dtv-userland-core did not define the optional dtv::host_runtime target")
        endif()
        get_target_property(_host_runtime_source_dir dtv_userland_host_runtime SOURCE_DIR)
        set(DTV_USERLAND_HOST_RUNTIME_SOURCE_DIR
            "${_host_runtime_source_dir}/adapters/host_runtime" PARENT_SCOPE)
    endif()
    if(WIN32 AND PX4_BUILD_WINSCARD AND NOT TARGET dtv::windows_winscard_client)
        message(FATAL_ERROR
            "dtv-userland-core did not define the optional Windows WinSCard adapter")
    endif()
endfunction()
