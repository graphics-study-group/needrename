# 3.21 is the floor for install(TARGETS ... RUNTIME_DEPENDENCIES), which the
# install rules use to bundle the required shared libraries next to the
# executables.
cmake_minimum_required(VERSION 3.21)

# TODO: rename - the project name is one of the rename points (the placeholder
# package identity is `needrename`; see the external-project-consumption spec).
project(hello_sdl3)
set(CMAKE_CXX_STANDARD 20)

# --- Build feature options ------------------------------------------------
# Each option gates a subsystem at configure time. Every default except
# ENGINE_INSTALL_TESTS is ON, so an unmodified configure builds the same
# targets as before the options existed.
option(ENGINE_BUILD_TESTS "Build first-party tests and vendored third-party tests" ON)
option(ENGINE_BUILD_EXAMPLES "Build the example executables" ON)
option(ENGINE_BUILD_APPS "Build the editor and physics applications" ON)
option(ENGINE_BUILD_TOOLS "Build build-time helper executables (e.g. shader_baker)" ON)
option(ENGINE_BUILD_DOCS "Generate the doxygen documentation" ON)
option(ENGINE_INSTALL_TESTS "Install test executables and their data" OFF)

set(ENGINE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ENGINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(ENGINE_EDITOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/app/editor)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(BUILTIN_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/builtin_assets)
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

# On Linux, executables in bin/ must find sibling .so files (the $ORIGIN RPATH)
# the same way Windows finds DLLs in the executable directory.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

include(CTest)

find_package(Vulkan REQUIRED COMPONENTS glslangValidator)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
message(STATUS "Vulkan version: ${Vulkan_VERSION}")

add_subdirectory(third_party)

# Debug options
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-g>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-O0>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wall>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wextra>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wpedantic>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Weffc++>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/Zi>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/Od>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/W4>)
# add_compile_options($<$<CONFIG:Debug>:-pg>)
# add_link_options($<$<CONFIG:Debug>:-pg>)

# Release options
add_compile_options($<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang>>:-O3>)
add_compile_options($<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>)
add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)

# MSVC: /Zc:__cplusplus so __cplusplus reflects the -std=c++20 standard level
# instead of defaulting to 199711L.
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>)

add_subdirectory(engine)

if (ENGINE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
if (ENGINE_BUILD_APPS)
    add_subdirectory(app)
endif()
if (ENGINE_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()
if (ENGINE_BUILD_TESTS)
    add_subdirectory(test)
endif()

# TODO: rename - the legacy top-level shader pipeline is removed by the
# shader-distribution work; kept configured until then.
add_subdirectory(shader)
