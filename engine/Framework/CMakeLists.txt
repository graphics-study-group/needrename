file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibFramework
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibFramework 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
    EnginePhysics
)
target_link_libraries(EngineLibFramework
    PUBLIC
    tinyxml2
    tinyobjloader
    stb
    fastgltf::fastgltf
)
