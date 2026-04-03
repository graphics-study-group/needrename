file(GLOB_RECURSE SOURCE CONFIGURE_DEPENDS "./*.cpp")

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
    assimp::assimp
    tinyobjloader
    stb
)
