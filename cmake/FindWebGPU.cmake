# Locate the standard WebGPU C interface (`webgpu.h`) and a library behind it.
#
# Two shapes are supported, and both produce the same imported target
# `WebGPU::WebGPU` so nothing downstream has to know which one is in use:
#
#   * the browser build, where the interface and its implementation come from
#     the Emscripten port `emdawnwebgpu` and no import library exists;
#   * the desktop validation build, where a WebGPU implementation
#     (wgpu-native or Dawn) is installed in `WEBGPU_ROOT`.
#
# Search order for the desktop shape:
#   1. -DWEBGPU_ROOT=<prefix>            (cache variable)
#   2. $ENV{WEBGPU_ROOT}
#   3. $ENV{WEBGPU_SDK}, $ENV{DAWN_ROOT}
#
# The imported target exposes WEBGPU_HEADER_DIR (the directory holding
# `webgpu/webgpu.h`) as an interface include directory and the implementation
# library as an interface link library.
#
# Defines:
#   WebGPU_FOUND
#   WebGPU_HEADER_DIR    directory containing `webgpu/webgpu.h`
#   WebGPU_LIBRARY       implementation library, empty for the browser shape
#   WebGPU_IS_BROWSER    TRUE when the interface comes from the Emscripten port

include(FindPackageHandleStandardArgs)

if (EMSCRIPTEN)
    set(WebGPU_IS_BROWSER TRUE)
    set(WebGPU_HEADER_DIR "")
    set(WebGPU_LIBRARY "")
    if (NOT TARGET WebGPU::WebGPU)
        add_library(WebGPU::WebGPU INTERFACE IMPORTED)
        # The port supplies both the headers and the JavaScript glue. It is a
        # link-time port, so it is requested as a link option.
        set_target_properties(WebGPU::WebGPU PROPERTIES
            INTERFACE_LINK_OPTIONS "--use-port=emdawnwebgpu"
        )
    endif()
    find_package_handle_standard_args(WebGPU
        REQUIRED_VARS WebGPU_IS_BROWSER
        REASON_FAILURE_MESSAGE "the Emscripten WebGPU port is requested by the toolchain"
    )
    return()
endif()

set(WebGPU_IS_BROWSER FALSE)

set(_webgpu_hints "")
foreach(_hint IN ITEMS "${WEBGPU_ROOT}" "$ENV{WEBGPU_ROOT}" "$ENV{WEBGPU_SDK}" "$ENV{DAWN_ROOT}")
    if (_hint)
        list(APPEND _webgpu_hints "${_hint}")
    endif()
endforeach()

find_path(WebGPU_HEADER_DIR
    NAMES webgpu/webgpu.h webgpu.h
    HINTS ${_webgpu_hints}
    PATH_SUFFIXES include include/webgpu inc
    DOC "Directory containing the standard WebGPU C header `webgpu/webgpu.h`"
)

# wgpu-native ships `wgpu_native.lib` next to `wgpu_native.dll`; Dawn ships
# `webgpu_dawn.lib`. Either satisfies the desktop validation build.
find_library(WebGPU_LIBRARY
    NAMES wgpu_native webgpu_dawn webgpu_dawn.dll webgpu
    HINTS ${_webgpu_hints}
    PATH_SUFFIXES lib lib64 bin
    DOC "WebGPU implementation library for the desktop validation build"
)

find_package_handle_standard_args(WebGPU
    REQUIRED_VARS WebGPU_HEADER_DIR WebGPU_LIBRARY
    REASON_FAILURE_MESSAGE
        "set WEBGPU_ROOT to a wgpu-native or Dawn installation, or configure with EMSCRIPTEN for the browser shape"
)

if (WebGPU_FOUND AND NOT TARGET WebGPU::WebGPU)
    add_library(WebGPU::WebGPU INTERFACE IMPORTED)
    # The header is included as <webgpu/webgpu.h>; the found directory is the
    # one that holds that relative path.
    get_filename_component(_webgpu_include_parent "${WebGPU_HEADER_DIR}" DIRECTORY)
    if (EXISTS "${WebGPU_HEADER_DIR}/webgpu/webgpu.h")
        set(_webgpu_include_dir "${WebGPU_HEADER_DIR}")
    elseif (EXISTS "${_webgpu_include_parent}/webgpu/webgpu.h")
        set(_webgpu_include_dir "${_webgpu_include_parent}")
    else()
        set(_webgpu_include_dir "${WebGPU_HEADER_DIR}")
    endif()
    set_target_properties(WebGPU::WebGPU PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_webgpu_include_dir}"
        INTERFACE_LINK_LIBRARIES "${WebGPU_LIBRARY}"
    )
endif()
