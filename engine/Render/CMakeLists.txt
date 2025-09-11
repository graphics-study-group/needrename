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
    spirv-reflect
    spirv-cross-cpp
    vma
)
target_precompile_headers(EngineLibRender PRIVATE "FullRenderSystem.h")
target_compile_definitions(EngineLibRender PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
