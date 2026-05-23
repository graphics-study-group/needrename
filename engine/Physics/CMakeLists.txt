file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibPhysics
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibPhysics
    PRIVATE
    EngineLibHeaderInterface
    EngineLibExternalDependency
)
