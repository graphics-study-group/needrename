# Only the selected backend is compiled. Everything outside Backend/ is
# shared, and the reflection pass below covers only the public headers.
file(GLOB_RECURSE RHI_ALL_SOURCES "./*.cpp")
file(GLOB_RECURSE RHI_ALL_HEADERS "./*.h")
set(SOURCE "")
set(RHI_HEADERS "")
foreach(_src IN LISTS RHI_ALL_SOURCES)
    if (ENGINE_RHI_BACKEND_VULKAN AND _src MATCHES "/Backend/WebGPU/")
        continue()
    endif()
    if (ENGINE_RHI_BACKEND_WEBGPU AND _src MATCHES "/Backend/Vulkan/")
        continue()
    endif()
    # SPLayout's two implementations are the one place where what this build contains,
    # rather than which backend it targets, decides: the reflection unit is needed by a
    # backend that reflects at runtime and by a build that contains the shader baker, and
    # the stub is what is left. See ENGINE_RHI_BAKE_REFLECTION in the top-level file.
    if (_src MATCHES "Pipeline/SpirvShaderParameterLayout\\.cpp" AND NOT ENGINE_RHI_BAKE_REFLECTION
        AND NOT ENGINE_RHI_BACKEND_VULKAN)
        continue()
    endif()
    if (_src MATCHES "ShaderParameterLayoutUnavailable\\.cpp" AND ENGINE_RHI_BAKE_REFLECTION)
        continue()
    endif()
    list(APPEND SOURCE "${_src}")
endforeach()
foreach(_hdr IN LISTS RHI_ALL_HEADERS)
    if (_hdr MATCHES "/Backend/")
        continue()
    endif()
    list(APPEND RHI_HEADERS "${_hdr}")
endforeach()

# EngineRhi is the engine's generic GPU infrastructure layer, built as its own DLL
add_library(EngineRhi ${SOURCE})

# Whether this build can reflect a module at runtime is decided by which of SPLayout's two
# implementations it contains, and that is the question a caller wants answered: the bake-only
# macro means something different (that the shader baker is in the build).
if (ENGINE_RHI_BAKE_REFLECTION OR ENGINE_RHI_BACKEND_VULKAN)
    target_compile_definitions(EngineRhi PUBLIC ENGINE_RHI_HAS_REFLECTION=1)
endif()
target_link_libraries(EngineRhi
    PUBLIC
    EngineLibHeaderInterface
    EngineDepSdl
    AnnoRefl
)

if (ENGINE_RHI_BACKEND_VULKAN)
    target_link_libraries(EngineRhi
        PUBLIC
        EngineDepVulkanExport
    )
    target_link_libraries(EngineRhi
        PRIVATE
        spirv-cross-cpp
    )

    # export VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
    if (WIN32 AND (NOT MSVC))
        target_compile_definitions(EngineRhi PRIVATE "VULKAN_HPP_STORAGE_API=__declspec(dllexport)")
    else()
        target_compile_definitions(EngineRhi PRIVATE VULKAN_HPP_STORAGE_SHARED=1 VULKAN_HPP_STORAGE_SHARED_EXPORT=1)
    endif()
else()
    # The WebGPU backend reaches the standard WebGPU C interface only. It has no
    # Vulkan SDK, VMA or glslang dependency, and reflects nothing at runtime: the
    # shader interface description is baked alongside the WGSL. A desktop build that
    # contains the baker compiles the reflection unit for the baker's sake, and that
    # is the one case where SPIRV-Cross is linked here.
    target_link_libraries(EngineRhi PUBLIC EngineDepWebgpu)
    if (ENGINE_RHI_BAKE_REFLECTION)
        target_link_libraries(EngineRhi PRIVATE spirv-cross-cpp)
    endif()
endif()
target_compile_definitions(EngineRhi PRIVATE RHI_DLL_EXPORTS)

# Reflection: per-DLL registration for the REFL_SER_CLASS types (ImageFormat, PipelineEnums, ...).
include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(RHI_HEADERS RHI_REFLECTION_HEADERS)

set(all_include_dirs)
get_include_directories_for_target(EngineRhi all_include_dirs)

add_reflection_parser(
    target_name meta_rhi
    reflection_search_files "${RHI_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}"
)
add_dependencies(EngineRhi meta_rhi)
target_link_libraries(EngineRhi PRIVATE meta_rhi)


