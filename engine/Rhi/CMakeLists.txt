file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE RHI_HEADERS "./*.h")

add_library(EngineDepVulkanExport INTERFACE)
target_link_libraries(EngineDepVulkanExport INTERFACE Vulkan::Vulkan vma glslang)
target_compile_definitions(EngineDepVulkanExport INTERFACE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

# EngineRhi is the engine's generic GPU infrastructure layer, built as its own DLL
add_library(EngineRhi SHARED ${SOURCE})
target_link_libraries(EngineRhi
    PUBLIC
    EngineLibHeaderInterface
    EngineDepSdl
    EngineDepVulkanExport
    AnnoRefl
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
