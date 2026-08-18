cmake_minimum_required(VERSION 3.16)

project(hello_sdl3)
set(CMAKE_CXX_STANDARD 20)

set(ENGINE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ENGINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(ENGINE_EDITOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/app/editor)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(BUILTIN_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/builtin_assets)
set(ANROREFL_PARSER_ENV_DIR "${CMAKE_SOURCE_DIR}/build/parser_env" CACHE STRING "Path to AnnoRefl parser Python virtualenv")
set(EXAMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/example)
set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
set(PROJECTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/projects)
set(BUILD_SHARED_LIBS ON)

# Unified output directories: all executables and shared libraries go to bin/,
# static/import libraries go to lib/. This keeps .exe and .dll in the same
# directory so the OS loader can find them without manual copy steps.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(CTest)

find_package(Vulkan REQUIRED COMPONENTS glslangValidator)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
message(STATUS "Vulkan version: ${Vulkan_VERSION}")

add_subdirectory(third_party)

# Debug options
add_compile_options($<$<CONFIG:Debug>:-g>)
add_compile_options($<$<CONFIG:Debug>:-O0>)
add_compile_options($<$<CONFIG:Debug>:-Wall>)
add_compile_options($<$<CONFIG:Debug>:-Wextra>)
add_compile_options($<$<CONFIG:Debug>:-Wpedantic>)
add_compile_options($<$<CONFIG:Debug>:-Weffc++>)
# add_compile_options($<$<CONFIG:Debug>:-pg>)
# add_link_options($<$<CONFIG:Debug>:-pg>)

# Release options
add_compile_options($<$<CONFIG:Release>:-O3>)
add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)

add_subdirectory(engine)
add_subdirectory(app)
add_subdirectory(example)
add_subdirectory(shader)

add_subdirectory(test)
