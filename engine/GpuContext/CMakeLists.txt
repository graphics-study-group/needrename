file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE GPU_CONTEXT_HEADERS "./*.h")

add_library(GpuContext SHARED ${SOURCE})
target_compile_definitions(GpuContext PRIVATE GPU_CONTEXT_DLL_EXPORTS VULKAN_HPP_STORAGE_SHARED_EXPORT)
target_link_libraries(GpuContext
    PUBLIC
    EngineLibHeaderInterface
    Core
    EngineDepSdl
    EngineDepVulkan
)
