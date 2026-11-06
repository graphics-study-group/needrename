cmake_minimum_required(VERSION 3.21)

# TODO: rename - the project name, the CMake package/config name and the
# `needrename::` namespace prefix are the rename points. The placeholder
# identity is `needrename` (see the external-project-consumption spec).
project(needrename VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 20)

# --- Build feature options ------------------------------------------------
option(ENGINE_BUILD_TESTS "Build first-party tests and vendored third-party tests" ON)
option(ENGINE_BUILD_EXAMPLES "Build the example executables" ON)
option(ENGINE_BUILD_APPS "Build the editor and physics applications" ON)
option(ENGINE_BUILD_TOOLS "Build build-time helper executables (e.g. shader_baker)" ON)
option(ENGINE_BUILD_WEBGPU_SPIKE "Build the WebGPU spike harness (measurement tool, not a product target)" OFF)
option(ENGINE_BUILD_DOCS "Generate the doxygen documentation" ON)
option(ENGINE_INSTALL_TESTS "Install test executables and their data" OFF)

# A browser cross-build consumes native tools from this minimal host build.
option(ENGINE_HOST_TOOLS_ONLY "Configure only the native host shader tools" OFF)
if (ENGINE_HOST_TOOLS_ONLY)
    set(ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ENGINE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ENGINE_BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(ENGINE_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(ENGINE_BUILD_WEBGPU_SPIKE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

# --- RHI backend selection -------------------------------------------------
set(ENGINE_RHI_BACKEND "vulkan" CACHE STRING "RHI backend compiled into the build (vulkan|webgpu)")
set_property(CACHE ENGINE_RHI_BACKEND PROPERTY STRINGS vulkan webgpu)
if (NOT ENGINE_RHI_BACKEND MATCHES "^(vulkan|webgpu)$")
    message(FATAL_ERROR
        "ENGINE_RHI_BACKEND must be one of vulkan, webgpu (got '${ENGINE_RHI_BACKEND}').")
endif()
if (ENGINE_RHI_BACKEND STREQUAL "vulkan")
    set(ENGINE_RHI_BACKEND_VULKAN ON)
    set(ENGINE_RHI_BACKEND_WEBGPU OFF)
    set(ENGINE_SHADER_COMPILER_HAS_GLSLANG ON)
    add_compile_definitions(ENGINE_RHI_BACKEND_VULKAN=1 ENGINE_SHADER_COMPILER_HAS_GLSLANG=1)
else()
    set(ENGINE_RHI_BACKEND_VULKAN OFF)
    set(ENGINE_RHI_BACKEND_WEBGPU ON)
    add_compile_definitions(ENGINE_RHI_BACKEND_WEBGPU=1)
endif()
message(STATUS "RHI backend: ${ENGINE_RHI_BACKEND}")

# --- Source and output layout ---------------------------------------------
set(ENGINE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ENGINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(ENGINE_EDITOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/app/editor)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(BUILTIN_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/builtin_assets)
set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
set(PROJECTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/projects)

if (NOT EMSCRIPTEN)
    set(BUILD_SHARED_LIBS ON)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
if (UNIX)
    set(CMAKE_INSTALL_RPATH "\$ORIGIN")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)
include(GNUInstallDirs)

# --- External dependencies -------------------------------------------------
if (ENGINE_RHI_BACKEND_VULKAN AND NOT ENGINE_HOST_TOOLS_ONLY)
    find_package(Vulkan REQUIRED)
    message(STATUS "Vulkan version: ${Vulkan_VERSION}")
endif()
if (EMSCRIPTEN)
    include(cmake/EmscriptenSdl3.cmake)
else()
    find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3-shared)
endif()
include(cmake/WebGPUDependencies.cmake)

add_subdirectory(third_party)

# CMake's configuration flags already supply debug symbols, optimization and
# NDEBUG; only project-specific diagnostics and MSVC conformance are added here.
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wall>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wextra>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Wpedantic>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-Weffc++>)
add_compile_options($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/W4>)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>)

# Shader tooling must exist before the engine declares its shader pipelines.
if (NOT EMSCRIPTEN)
    add_subdirectory(tools/shader_baker)
endif()
include(cmake/ShaderTooling.cmake)

if (NOT ENGINE_HOST_TOOLS_ONLY)
    set(ENGINE_DATA_STAGE_DIR "${CMAKE_BINARY_DIR}/stage/$<CONFIG>")
    add_subdirectory(engine)
    include(cmake/RuntimeData.cmake)
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

if (NOT ENGINE_HOST_TOOLS_ONLY)
    include(cmake/install_rules.cmake)
endif()
