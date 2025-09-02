message("COMPILE engine library")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

list(FILTER SOURCE EXCLUDE REGEX "__generated__/.*")
list(FILTER HEADER EXCLUDE REGEX "__generated__/.*")
list(FILTER SOURCE EXCLUDE REGEX "Tests/.*")
list(FILTER HEADER EXCLUDE REGEX "Tests/.*")

list(FILTER SOURCE EXCLUDE REGEX "Asset/.*")
list(FILTER SOURCE EXCLUDE REGEX "Reflection/.*")
list(FILTER SOURCE EXCLUDE REGEX "Render/.*")

# Header files and some deps are shared across these modules, so we use an interface to manage them
add_library(EngineLibHeaderInterface INTERFACE)
target_include_directories(EngineLibHeaderInterface INTERFACE ${ENGINE_SOURCE_DIR})
target_link_libraries(EngineLibHeaderInterface 
    INTERFACE
    SDL3::SDL3
    Vulkan::Vulkan
    glm
    json
    imgui
)

add_subdirectory(Asset)
add_subdirectory(Reflection)
add_subdirectory(Render)

add_library(engine SHARED 
    ${SOURCE}
    $<TARGET_OBJECTS:EngineLibReflection>
    $<TARGET_OBJECTS:EngineLibRender>
    $<TARGET_OBJECTS:EngineLibAsset>
)
target_link_libraries(engine
    PUBLIC
    EngineLibHeaderInterface
)
target_link_libraries(engine 
    PUBLIC
    EngineLibReflection
    EngineLibRender
    EngineLibAsset
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

add_subdirectory(Tests)
