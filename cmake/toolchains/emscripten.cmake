# Emscripten cross-compilation toolchain for the browser (WebGPU) build.
#
# Resolves the SDK from, in order:
#   1. -DEMSDK_ROOT=<prefix>            (cache variable)
#   2. $ENV{EMSDK}
#   3. $ENV{EMSDK_ROOT}
#
# then defers to the SDK's own platform module, which sets the compilers, the
# sysroot, the WASM linker rules and the port mechanism. Everything the engine
# needs beyond that (the WebGPU interface) is asked for as a link-time port by
# cmake/FindWebGPU.cmake, so this file stays a plain locator.
#
# The build is a browser build: executables are .html + .wasm + .js, and every
# target that reaches the device links `--use-port=emdawnwebgpu`.

set(_emscripten_candidates "")
if (EMSDK_ROOT)
    list(APPEND _emscripten_candidates "${EMSDK_ROOT}")
endif()
if (DEFINED ENV{EMSDK})
    list(APPEND _emscripten_candidates "$ENV{EMSDK}")
endif()
if (DEFINED ENV{EMSDK_ROOT})
    list(APPEND _emscripten_candidates "$ENV{EMSDK_ROOT}")
endif()

set(_emscripten_platform "")
foreach(_candidate IN LISTS _emscripten_candidates)
    if (EXISTS "${_candidate}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
        set(_emscripten_platform "${_candidate}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
        set(EMSDK_ROOT "${_candidate}" CACHE PATH "Emscripten SDK root" FORCE)
        break()
    endif()
endforeach()

if (NOT _emscripten_platform)
    message(FATAL_ERROR
        "Emscripten SDK not found. Set EMSDK_ROOT (or the EMSDK environment variable) to an "
        "activated emsdk checkout that contains upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake.")
endif()

message(STATUS "Emscripten toolchain: ${_emscripten_platform}")
include("${_emscripten_platform}")

# The WebGPU build is a browser build: a shared-library product is not a thing
# in wasm, and the engine's modules become static archives linked into the
# final .html.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Emscripten builds static archives" FORCE)
