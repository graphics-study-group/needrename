file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(Reflection SHARED ${SOURCE})
target_compile_definitions(Reflection PRIVATE REFLECTION_DLL_EXPORTS)
target_link_libraries(Reflection
    PUBLIC
    EngineLibHeaderInterface
    EngineDepGlm
    EngineDepJson
)
