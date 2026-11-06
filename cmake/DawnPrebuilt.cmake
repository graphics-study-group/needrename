include_guard(GLOBAL)

# Resolve a native Dawn package. Explicit package directories and prefixes always
# take precedence; only an unspecified prefix triggers the pinned download.
set(DAWN_HAS_DEBUG OFF)
set(DAWN_TINT_PREFIX)

function(_dawn_read_imported_configurations)
    get_target_property(_dawn_configs dawn::webgpu_dawn IMPORTED_CONFIGURATIONS)
    if (_dawn_configs AND "DEBUG" IN_LIST _dawn_configs)
        set(DAWN_HAS_DEBUG ON PARENT_SCOPE)
    endif()
endfunction()

function(_dawn_find_package prefix)
    if (POLICY CMP0144)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0144 NEW)
    endif()
    find_package(Dawn CONFIG REQUIRED
        HINTS "${prefix}"
        PATH_SUFFIXES lib/cmake/Dawn
        NO_DEFAULT_PATH
    )
    if (POLICY CMP0144)
        cmake_policy(POP)
    endif()
endfunction()

function(_dawn_fetch_prebuilt config url sha256 destination output_var)
    if (EXISTS "${destination}/.dawn-complete")
        set(${output_var} "${destination}" PARENT_SCOPE)
        return()
    endif()

    set(_download_dir "${CMAKE_SOURCE_DIR}/build/_deps/dawn/v20261105.214757/downloads")
    set(_archive "${_download_dir}/Dawn-${config}.tar.gz")
    set(_download "${_archive}.download")
    file(MAKE_DIRECTORY "${_download_dir}")

    if (EXISTS "${_archive}")
        file(SHA256 "${_archive}" _actual_sha256)
        if (NOT _actual_sha256 STREQUAL sha256)
            message(WARNING "Discarding the corrupt Dawn ${config} archive at ${_archive}")
            file(REMOVE "${_archive}")
        endif()
    endif()
    if (NOT EXISTS "${_archive}")
        message(STATUS "Downloading Dawn ${config} (${url})")
        file(REMOVE "${_download}")
        file(DOWNLOAD "${url}" "${_download}"
            HTTPHEADER "Accept: application/octet-stream"
            EXPECTED_HASH "SHA256=${sha256}"
            STATUS _download_status
            SHOW_PROGRESS
        )
        list(GET _download_status 0 _download_result)
        if (NOT _download_result EQUAL 0)
            list(GET _download_status 1 _download_error)
            file(REMOVE "${_download}")
            message(FATAL_ERROR "Dawn ${config} download failed: ${_download_error}")
        endif()
        file(RENAME "${_download}" "${_archive}")
    endif()

    if (config STREQUAL "debug")
        set(_config_title Debug)
    else()
        set(_config_title Release)
    endif()
    set(_archive_prefix "Dawn-dfc836b2dea35488fd0488302b27cd47f0ffcec1-windows-latest-${_config_title}")
    set(_extract_dir "${destination}.extracting")
    set(_extract_prefix "${_extract_dir}/${_archive_prefix}")
    file(REMOVE_RECURSE "${_extract_dir}" "${destination}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_extract_dir}")
    if (NOT EXISTS "${_extract_prefix}/lib/cmake/Dawn/DawnConfig.cmake"
            OR NOT EXISTS "${_extract_prefix}/lib/webgpu_dawn.lib"
            OR NOT EXISTS "${_extract_prefix}/bin/tint.exe")
        file(REMOVE_RECURSE "${_extract_dir}")
        message(FATAL_ERROR "The Dawn ${config} archive does not contain the expected prefix layout.")
    endif()
    file(WRITE "${_extract_prefix}/.dawn-complete" "v20261105.214757\n")
    file(RENAME "${_extract_prefix}" "${destination}")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(REMOVE "${_archive}")
    set(${output_var} "${destination}" PARENT_SCOPE)
endfunction()

set(_dawn_explicit_dir "${Dawn_DIR}")
if (Dawn_DIR AND DAWN_MANAGED_DIR AND Dawn_DIR STREQUAL DAWN_MANAGED_DIR)
    set(_dawn_explicit_dir)
endif()
if (_dawn_explicit_dir)
    unset(DAWN_MANAGED_DIR CACHE)
    get_filename_component(DAWN_TINT_PREFIX "${_dawn_explicit_dir}/../../.." ABSOLUTE)
    _dawn_find_package("${DAWN_TINT_PREFIX}")
    _dawn_read_imported_configurations()
    return()
endif()

set(_dawn_root "${DAWN_ROOT}")
if (NOT _dawn_root)
    set(_dawn_root "$ENV{DAWN_ROOT}")
endif()

if (_dawn_root)
    if (IS_DIRECTORY "${_dawn_root}/debug" OR IS_DIRECTORY "${_dawn_root}/release")
        set(_dawn_debug_prefix "${_dawn_root}/debug")
        set(_dawn_release_prefix "${_dawn_root}/release")
        foreach(_config IN ITEMS debug release)
            set(_prefix "${_dawn_root}/${_config}")
            if (NOT EXISTS "${_prefix}/lib/cmake/Dawn/DawnConfig.cmake"
                    OR NOT EXISTS "${_prefix}/lib/webgpu_dawn.lib")
                message(FATAL_ERROR
                    "DAWN_ROOT '${_dawn_root}' has an incomplete ${_config} package. Expected "
                    "lib/cmake/Dawn/DawnConfig.cmake and lib/webgpu_dawn.lib.")
            endif()
        endforeach()
    elseif (EXISTS "${_dawn_root}/lib/cmake/Dawn/DawnConfig.cmake")
        set(_dawn_release_prefix "${_dawn_root}")
    else()
        message(FATAL_ERROR
            "DAWN_ROOT '${_dawn_root}' is not a Dawn prefix. Provide the single-package layout, "
            "or matching debug/ and release/ subdirectories; leave DAWN_ROOT unset to download the pinned packages.")
    endif()
else()
    if (NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR
            "Automatic Dawn download currently supports only native Windows MSVC builds. "
            "Set DAWN_ROOT or Dawn_DIR to a matching Dawn package.")
    endif()
    # Fixed GitHub asset IDs select the same revisioned archives as the release
    # page; the API endpoint also supports reliable ranged downloads.
    set(_dawn_download_root "${CMAKE_SOURCE_DIR}/build/_deps/dawn/v20261105.214757")
    _dawn_fetch_prebuilt(
        debug
        "https://api.github.com/repos/google/dawn/releases/assets/716405237"
        "273a560689041392cda5b2ad574b22758ef0862db312d100e7d070e3d005ba9d"
        "${_dawn_download_root}/debug"
        _dawn_debug_prefix
    )
    _dawn_fetch_prebuilt(
        release
        "https://api.github.com/repos/google/dawn/releases/assets/716405383"
        "de26b4ec845662e1e2d2d6053e570f2442eb8b1cbcb0ac4ac35c6a44f366dec5"
        "${_dawn_download_root}/release"
        _dawn_release_prefix
    )
endif()

# Both packages in a dual-config layout come from the same revision. Loading the
# Release export supplies Dawn's transitive link interface; the separate Debug
# library only adds another imported configuration to that same target.
set(Dawn_DIR "${_dawn_release_prefix}/lib/cmake/Dawn" CACHE PATH "Dawn package directory" FORCE)
set(DAWN_MANAGED_DIR "${Dawn_DIR}" CACHE INTERNAL "Dawn directory resolved by DawnPrebuilt.cmake" FORCE)
_dawn_find_package("${_dawn_release_prefix}")
set(DAWN_TINT_PREFIX "${_dawn_release_prefix}")
if (_dawn_debug_prefix)
    set_property(TARGET dawn::webgpu_dawn APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
    set_target_properties(dawn::webgpu_dawn PROPERTIES
        IMPORTED_LOCATION_DEBUG "${_dawn_debug_prefix}/lib/webgpu_dawn.lib"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
        MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
    )
    set(DAWN_HAS_DEBUG ON)
else()
    _dawn_read_imported_configurations()
endif()
