file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibRender
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibRender 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
)
target_link_libraries(EngineLibRender
    PUBLIC
    spirv-cross-cpp
    vma
)
target_precompile_headers(EngineLibRender PRIVATE "FullRenderSystem.h")

# Export defaultDispatchLoaderDynamic
target_compile_definitions(EngineLibRender PRIVATE VULKAN_HPP_STORAGE_SHARED_EXPORT)
