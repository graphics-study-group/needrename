file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER_PRIVATE "./*.hpp")

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
    ktx
)
target_precompile_headers(EngineLibRender PRIVATE "FullRenderSystem.h")
