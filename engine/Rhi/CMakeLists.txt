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
    # Vulkan SDK, VMA, glslang or SPIRV-Cross dependency, and reflects nothing at
    # runtime: the shader interface description is baked alongside the WGSL.
    target_link_libraries(EngineRhi PUBLIC EngineDepWebgpu)
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


