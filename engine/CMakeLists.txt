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
    vma
    glslang
)
target_compile_definitions(EngineLibExternalDependency INTERFACE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_definitions(EngineLibExternalDependency INTERFACE VULKAN_HPP_STORAGE_SHARED)
target_compile_definitions(EngineLibExternalDependency INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_subdirectory(Asset)
add_subdirectory(Core)
add_subdirectory(Framework)
add_subdirectory(Reflection)
add_subdirectory(Render)
add_subdirectory(UserInterface)

set(ENGINE_LIBRARY_TYPE STATIC)
if(ENGINE_BUILD_SHARED)
    set(ENGINE_LIBRARY_TYPE SHARED)
endif()

add_library(engine ${ENGINE_LIBRARY_TYPE}
    "MainClass.cpp"
    $<TARGET_OBJECTS:imgui>
    $<TARGET_OBJECTS:EngineLibAsset>
    $<TARGET_OBJECTS:EngineLibCore>
    $<TARGET_OBJECTS:EngineLibFramework>
    $<TARGET_OBJECTS:EngineLibReflection>
    $<TARGET_OBJECTS:EngineLibRender>
    $<TARGET_OBJECTS:EngineLibUserInterface>
)

if(ENGINE_BUILD_SHARED)
    set_target_properties(engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # `WINDOWS_EXPORT_ALL_SYMBOLS` is not reflected into the GNU-on-Windows link line here,
        # so the generated import library misses many C++ symbols used by tests/examples.
        target_link_options(engine PRIVATE "-Wl,--export-all-symbols")
    endif()
endif()

add_dependencies(engine shader)

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

# TODO: Use relative paths when install.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h)

include(CTest)
add_subdirectory(Tests)
