file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibFramework
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibFramework 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
)
