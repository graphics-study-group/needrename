file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibAsset
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibAsset 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
)
target_link_libraries(EngineLibAsset
    PUBLIC
    tinyobjloader
    stb
)
