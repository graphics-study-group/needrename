add_subdirectory(AnnoRefl)

# disable warning for third party
add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-w>$<$<CXX_COMPILER_ID:MSVC>:/w>)

# Host-tools builds need only what the native shader tools link: JSON, glslang
# and SPIRV-Cross. Everything else (rendering, UI, asset formats) is engine
# runtime territory and stays out of the host-tools tree.
if (ENGINE_HOST_TOOLS_ONLY)
    add_subdirectory(glm EXCLUDE_FROM_ALL SYSTEM)
    add_subdirectory(json EXCLUDE_FROM_ALL SYSTEM)

    set(ENABLE_OPT OFF CACHE BOOL "Enable optimizations" FORCE)
    add_subdirectory(glslang EXCLUDE_FROM_ALL SYSTEM)

    set(SPIRV_CROSS_CLI OFF CACHE BOOL "Build the CLI binary. Requires SPIRV_CROSS_STATIC." FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "Enable SPIRV-Cross tests." FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "Enable HLSL target support." FORCE)
    set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "Enable MSL target support." FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "Enable C API wrapper support in static library." FORCE)
    set(SPIRV_CROSS_FORCE_PIC ON CACHE BOOL "Force position-independent code for all targets." FORCE)
    add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL SYSTEM)
    return()
endif()

add_subdirectory(glm EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(glm PROPERTIES FOLDER third_party)

add_subdirectory(json EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(json PROPERTIES FOLDER third_party)

add_subdirectory(stb EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(stb PROPERTIES FOLDER third_party)

# VMA is the Vulkan memory allocator: only the Vulkan backend uses it.
if (ENGINE_RHI_BACKEND_VULKAN)
    add_subdirectory(vma EXCLUDE_FROM_ALL SYSTEM)
    set_target_properties(vma PROPERTIES FOLDER third_party)
endif()

add_subdirectory(tinyobjloader EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(tinyobjloader PROPERTIES FOLDER third_party)

# tinyxml2: lightweight XML parser (2 source files)
add_subdirectory(tinyxml2 EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(tinyxml2 PROPERTIES FOLDER third_party)

set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "Compile fastgltf as C++20" FORCE)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "Disable fastgltf tests" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "Disable fastgltf examples" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "Disable fastgltf docs" FORCE)
set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "Disable fastgltf install targets" FORCE)
add_subdirectory(fastgltf EXCLUDE_FROM_ALL SYSTEM)
set_target_properties(fastgltf PROPERTIES FOLDER third_party)
set_target_properties(fastgltf PROPERTIES POSITION_INDEPENDENT_CODE ON)

# SPIRV-Cross reflects SPIR-V modules at build time: the native shader tooling
# (and the Vulkan backend, which also reflects at runtime) need it on every
# native build. WebGPU runtime targets never link it; browser builds skip it.
if (NOT EMSCRIPTEN)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "Build the CLI binary. Requires SPIRV_CROSS_STATIC." FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "Enable SPIRV-Cross tests." FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "Enable HLSL target support." FORCE)
    set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "Enable MSL target support." FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "Enable C API wrapper support in static library." FORCE)
    set(SPIRV_CROSS_FORCE_PIC ON CACHE BOOL "Force position-independent code for all targets." FORCE)
    add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL SYSTEM)

    set_target_properties(spirv-cross-core PROPERTIES FOLDER third_party)
    set_target_properties(spirv-cross-cpp PROPERTIES FOLDER third_party)
    set_target_properties(spirv-cross-glsl PROPERTIES FOLDER third_party)
    set_target_properties(spirv-cross-reflect PROPERTIES FOLDER third_party)
    set_target_properties(spirv-cross-util PROPERTIES FOLDER third_party)
endif()

# Add ImGUI files. The renderer back-end is the RHI backend's own: the Vulkan
# one is compiled only for the Vulkan backend, the WebGPU one only for WebGPU.
if (ENGINE_RHI_BACKEND_VULKAN)
    set(IMGUI_RENDERER_BACKEND "${THIRD_PARTY_DIR}/imgui/backends/*_vulkan.cpp")
else()
    set(IMGUI_RENDERER_BACKEND "${THIRD_PARTY_DIR}/imgui/backends/*_wgpu.cpp")
endif()
file(GLOB IMGUI_SOURCE
    "${THIRD_PARTY_DIR}/imgui/*.cpp"
    ${IMGUI_RENDERER_BACKEND}
    "${THIRD_PARTY_DIR}/imgui/backends/*_sdl3.cpp"
)
message(DEBUG "Found ImGUI files: ${IMGUI_SOURCE}")
add_library(imgui EXCLUDE_FROM_ALL ${IMGUI_SOURCE})
set_target_properties(imgui PROPERTIES FOLDER third_party)
# imgui has no dllexport annotations, so on Windows it must be made to export
# its symbols explicitly (Linux .so exports by default).
if(MSVC)
    set_target_properties(imgui PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
if (ENGINE_RHI_BACKEND_VULKAN)
    target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)
else()
    target_link_libraries(imgui PUBLIC SDL3::SDL3 EngineDepWebgpu)
    # imgui's WebGPU back-end has to be told which implementation it talks to: the
    # standard interface and the implementation-specific headers differ enough that it asks
    # rather than guessing, and a build naming neither fails with an #error naming both.
    # The headers this build uses are Dawn's WebGPU headers (the browser shape uses the
    # emdawnwebgpu port's); imgui branches on which backend it is talking to, and the Dawn
    # branch is the one the WGPUComputeState / WGPUShaderSourceWGSL names belong to.
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN=1)
endif()
target_include_directories(
    imgui 
    SYSTEM PUBLIC 
    "${THIRD_PARTY_DIR}/imgui/"
    "${THIRD_PARTY_DIR}/imgui/backends/"
)

set(KTX_FEATURE_DOC OFF CACHE BOOL "Disable libktx docs" FORCE)
set(KTX_FEATURE_JNI OFF CACHE BOOL "Disable libktx JNI" FORCE)
set(KTX_FEATURE_PY OFF CACHE BOOL "Disable libktx python" FORCE)
set(KTX_FEATURE_TESTS OFF CACHE BOOL "Disable libktx tests" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "Disable libktx tools" FORCE)
set(KTX_FEATURE_JS OFF CACHE BOOL "Disable libktx JavaScript bindings" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE STRING "Disable libktx load test apps" FORCE)
set(LIBKTX_VERSION_FULL ON CACHE BOOL "Enable full libktx" FORCE)
set(LIBKTX_VERSION_READ_ONLY OFF CACHE BOOL "Disable read-only libktx" FORCE)
add_subdirectory(libktx EXCLUDE_FROM_ALL SYSTEM)
if(TARGET ktx)
    set_target_properties(ktx PROPERTIES FOLDER third_party)
    if(NOT MSVC)
        # libktx has a bug in `writer2.c` which causes a 64-bit ktx_size_t is writen as a 32-bit off_t.
        # So force 64-bit file offsets to avoid the issue.
        target_compile_definitions(ktx PUBLIC _FILE_OFFSET_BITS=64)
    endif()
endif()

set(ENABLE_OPT OFF CACHE BOOL "Enable optimizations" FORCE)
if (NOT EMSCRIPTEN)
    # glslang is the GLSL compiler of the native shader tooling on every native
    # build, and of the Vulkan backend at runtime; a browser build ships baked
    # SPIR-V/WGSL and compiles nothing, so it builds no glslang at all.
    add_subdirectory(glslang EXCLUDE_FROM_ALL SYSTEM)
endif()

# --- SDK install interfaces ------------------------------------------------
# The targets below are exported with the engine package, so their include
# directories must not point into this checkout. Headers are installed beneath
# <prefix>/include/needrename and the install interface names that location.
set(ANRO_SDK_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/needrename")

set_target_properties(glm PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
        "$<BUILD_INTERFACE:${THIRD_PARTY_DIR}/glm>;$<INSTALL_INTERFACE:${ANRO_SDK_INCLUDE_DIR}/glm>"
)
set_target_properties(json PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
        "$<BUILD_INTERFACE:${THIRD_PARTY_DIR}/json>;$<INSTALL_INTERFACE:${ANRO_SDK_INCLUDE_DIR}/json>"
)
if (TARGET vma)
    set_target_properties(vma PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
            "$<BUILD_INTERFACE:${THIRD_PARTY_DIR}/vma>;$<INSTALL_INTERFACE:${ANRO_SDK_INCLUDE_DIR}/vma>"
    )
endif()
set_target_properties(imgui PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES
        "$<BUILD_INTERFACE:${THIRD_PARTY_DIR}/imgui>;$<BUILD_INTERFACE:${THIRD_PARTY_DIR}/imgui/backends>;\
$<INSTALL_INTERFACE:${ANRO_SDK_INCLUDE_DIR}/imgui>;$<INSTALL_INTERFACE:${ANRO_SDK_INCLUDE_DIR}/imgui/backends>"
)
# Drop the SYSTEM copies: they duplicate the source-tree paths above.
set(_anro_system_reset_targets glm json imgui)
if (TARGET vma)
    list(APPEND _anro_system_reset_targets vma)
endif()
set_target_properties(${_anro_system_reset_targets} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "")

# With a Release-only desktop Dawn package, everything that can reach Dawn is
# switched to the release runtime by the top-level pass at the end of configure.
# Dual-config Dawn packages skip that pass. The shader-tooling third party (glslang,
# SPIRV-Cross) and libktx's encoder internals never link Dawn; they keep the
# default runtime, which is also what keeps libktx's mixed encoder sources
# internally consistent.
if (ENGINE_RHI_BACKEND_WEBGPU AND NOT EMSCRIPTEN)
    function(_engine_mark_default_runtime dir)
        get_property(_marked_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_marked_t IN LISTS _marked_targets)
            set_target_properties(${_marked_t} PROPERTIES ENGINE_DEFAULT_RUNTIME ON)
        endforeach()
        get_property(_marked_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(_marked_d IN LISTS _marked_subdirs)
            _engine_mark_default_runtime("${_marked_d}")
        endforeach()
    endfunction()
    _engine_mark_default_runtime(${CMAKE_CURRENT_SOURCE_DIR}/glslang)
    _engine_mark_default_runtime(${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Cross)
    _engine_mark_default_runtime(${CMAKE_CURRENT_SOURCE_DIR}/libktx)
endif()


