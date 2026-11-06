include_guard(GLOBAL)

# A browser build cannot run a baker built for its target. Use only an explicit
# override or the deterministic host-tools preset output, never another engine tree.
set(ENGINE_HOST_SHADER_BAKER "" CACHE FILEPATH "Host shader_baker executable for a cross build")
if (EMSCRIPTEN AND NOT ENGINE_HOST_SHADER_BAKER)
    if (CMAKE_HOST_WIN32)
        set(_host_baker_name shader_baker.exe)
    else()
        set(_host_baker_name shader_baker)
    endif()
    set(_host_baker "${CMAKE_SOURCE_DIR}/build/host-tools/bin/Release/${_host_baker_name}")
    if (EXISTS "${_host_baker}")
        set(ENGINE_HOST_SHADER_BAKER "${_host_baker}" CACHE FILEPATH
            "Host shader_baker executable for a cross build" FORCE)
    else()
        message(FATAL_ERROR
            "A browser build needs a native host shader_baker. Build it first with:\n"
            "    cmake --preset host-tools\n"
            "    cmake --build --preset host-tools\n"
            "or set ENGINE_HOST_SHADER_BAKER to an existing native shader_baker executable.")
    endif()
endif()
if (ENGINE_HOST_SHADER_BAKER)
    message(STATUS "Host shader_baker: ${ENGINE_HOST_SHADER_BAKER}")
endif()

# Native builds use the target created immediately before this file is included.
# Browser builds name the host executable and depend on its file so rebuilding the
# baker invalidates the shaders baked by it.
if (TARGET shader_baker)
    set(ENGINE_SHADER_BAKER_EXECUTABLE "$<TARGET_FILE:shader_baker>")
    set(ENGINE_SHADER_BAKER_DEPENDENCY shader_baker)
elseif (ENGINE_HOST_SHADER_BAKER AND EXISTS "${ENGINE_HOST_SHADER_BAKER}")
    set(ENGINE_SHADER_BAKER_EXECUTABLE "${ENGINE_HOST_SHADER_BAKER}")
    set(ENGINE_SHADER_BAKER_DEPENDENCY "${ENGINE_HOST_SHADER_BAKER}")
else()
    set(ENGINE_SHADER_BAKER_EXECUTABLE)
    set(ENGINE_SHADER_BAKER_DEPENDENCY)
endif()

# Built-in shader storage: auto uses GLSL for native Vulkan Debug and baked
# SPIR-V elsewhere. WebGPU always needs baked shaders because it has no runtime
# GLSL compiler.
set(ENGINE_SHADER_MODE "auto" CACHE STRING "Built-in shader storage form (auto|glsl|spv)")
set_property(CACHE ENGINE_SHADER_MODE PROPERTY STRINGS auto glsl spv)
if (NOT ENGINE_SHADER_MODE MATCHES "^(auto|glsl|spv)$")
    message(FATAL_ERROR "ENGINE_SHADER_MODE must be one of auto, glsl, spv (got '${ENGINE_SHADER_MODE}').")
endif()
if (ENGINE_SHADER_MODE STREQUAL "auto")
    if (ENGINE_RHI_BACKEND_WEBGPU)
        set(ENGINE_SHADER_EFFECTIVE_MODE "spv")
    else()
        set(ENGINE_SHADER_EFFECTIVE_MODE "$<IF:$<CONFIG:Debug>,glsl,spv>")
    endif()
else()
    set(ENGINE_SHADER_EFFECTIVE_MODE "${ENGINE_SHADER_MODE}")
endif()
if (NOT ENGINE_BUILD_TOOLS)
    if (ENGINE_SHADER_MODE STREQUAL "spv")
        message(FATAL_ERROR
            "ENGINE_SHADER_MODE=spv requires ENGINE_BUILD_TOOLS=ON: shader_baker performs the baking.")
    elseif (ENGINE_SHADER_MODE STREQUAL "auto")
        message(WARNING "ENGINE_BUILD_TOOLS=OFF: shipping built-in shaders in GLSL form.")
        set(ENGINE_SHADER_EFFECTIVE_MODE "glsl")
    endif()
endif()
