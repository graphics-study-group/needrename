file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibUserInterface
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibUserInterface 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
)
