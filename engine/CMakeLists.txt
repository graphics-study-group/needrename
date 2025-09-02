message("COMPILE engine library")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

list(FILTER SOURCE EXCLUDE REGEX "__generated__/.*")
list(FILTER HEADER EXCLUDE REGEX "__generated__/.*")
list(FILTER SOURCE EXCLUDE REGEX "Tests/.*")
list(FILTER HEADER EXCLUDE REGEX "Tests/.*")

list(FILTER SOURCE EXCLUDE REGEX "Asset/.*")
list(FILTER SOURCE EXCLUDE REGEX "Core/.*")
list(FILTER SOURCE EXCLUDE REGEX "Reflection/.*")
list(FILTER SOURCE EXCLUDE REGEX "Render/.*")
list(FILTER SOURCE EXCLUDE REGEX "UserInterface/.*")

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
    vma
)

add_subdirectory(Asset)
add_subdirectory(Core)
add_subdirectory(Framework)
add_subdirectory(Reflection)
add_subdirectory(Render)
add_subdirectory(UserInterface)

add_library(engine SHARED 
    "MainClass.cpp"
    $<TARGET_OBJECTS:EngineLibAsset>
    $<TARGET_OBJECTS:EngineLibCore>
    $<TARGET_OBJECTS:EngineLibFramework>
    $<TARGET_OBJECTS:EngineLibReflection>
    $<TARGET_OBJECTS:EngineLibRender>
    $<TARGET_OBJECTS:EngineLibUserInterface>
)
target_link_libraries(engine
    PUBLIC
    EngineLibHeaderInterface
)
target_link_libraries(engine 
    PUBLIC
    EngineLibAsset
    EngineLibCore
    EngineLibFramework
    EngineLibReflection
    EngineLibRender
    EngineLibUserInterface
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

add_subdirectory(Tests)
