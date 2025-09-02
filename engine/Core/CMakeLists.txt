file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibCore
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibCore 
    PRIVATE 
    EngineLibHeaderInterface
)
