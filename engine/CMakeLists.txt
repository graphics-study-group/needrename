message("COMPILE engine library")
file(GLOB_RECURSE HEADER "./*.h")

list(FILTER HEADER EXCLUDE REGEX "__generated__/.*")
list(FILTER HEADER EXCLUDE REGEX "Tests/.*")

# Header files and some deps are shared across these modules, so we use an interface to manage them
add_library(EngineLibHeaderInterface INTERFACE)
target_include_directories(EngineLibHeaderInterface INTERFACE ${ENGINE_SOURCE_DIR})
add_library(EngineLibExternalDependency INTERFACE)
target_link_libraries(EngineLibExternalDependency
    INTERFACE
    SDL3::SDL3
    Vulkan::Vulkan
    glm
    json
    imgui
)

add_subdirectory(Asset)
add_subdirectory(Core)
add_subdirectory(Framework)
add_subdirectory(Reflection)
add_subdirectory(Render)
add_subdirectory(UserInterface)

add_library(engine SHARED 
    "MainClass.cpp"
    $<TARGET_OBJECTS:imgui>
    $<TARGET_OBJECTS:EngineLibAsset>
    $<TARGET_OBJECTS:EngineLibCore>
    $<TARGET_OBJECTS:EngineLibFramework>
    $<TARGET_OBJECTS:EngineLibReflection>
    $<TARGET_OBJECTS:EngineLibRender>
    $<TARGET_OBJECTS:EngineLibUserInterface>
)

# Widely used dependencies such as SDL and Vulkan are marked as public
target_link_libraries(engine
    PUBLIC
    EngineLibHeaderInterface
    EngineLibExternalDependency
)

# Dependencies only used in a small module are marked as private to avoid pollution
target_link_libraries(engine 
    PRIVATE
    EngineLibAsset
    EngineLibCore
    EngineLibFramework
    EngineLibReflection
    EngineLibRender
    EngineLibUserInterface
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

add_subdirectory(Tests)
