# 3.21 is the floor for install(TARGETS ... RUNTIME_DEPENDENCIES), which the
# install rules use to bundle the required shared libraries next to the
# executables.
cmake_minimum_required(VERSION 3.21)

# TODO: rename - the project name, the CMake package/config name and the
# `needrename::` namespace prefix are the rename points. The placeholder
# identity is `needrename` (see the external-project-consumption spec).
project(needrename VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 20)

# --- Build feature options ------------------------------------------------
# Each option gates a subsystem at configure time. Every default except
# ENGINE_INSTALL_TESTS is ON, so an unmodified configure builds the same
# targets as before the options existed.
option(ENGINE_BUILD_TESTS "Build first-party tests and vendored third-party tests" ON)
option(ENGINE_BUILD_EXAMPLES "Build the example executables" ON)
option(ENGINE_BUILD_APPS "Build the editor and physics applications" ON)
option(ENGINE_BUILD_PYTHON_BINDINGS "Build the nanobind Python extension for PhysicsApp" ON)
option(ENGINE_BUILD_TOOLS "Build build-time helper executables (e.g. shader_baker)" ON)
option(ENGINE_BUILD_DOCS "Generate the doxygen documentation" ON)
option(ENGINE_INSTALL_TESTS "Install test executables and their data" OFF)

# Built-in shader storage form:
#   auto - glsl for Debug builds, spv for every other configuration
#   glsl - ship the GLSL sources plus the shader include tree, compiled at runtime
#   spv  - ship the shader archives with embedded SPIR-V
set(ENGINE_SHADER_MODE "auto" CACHE STRING "Built-in shader storage form (auto|glsl|spv)")
set_property(CACHE ENGINE_SHADER_MODE PROPERTY STRINGS auto glsl spv)
if (NOT ENGINE_SHADER_MODE MATCHES "^(auto|glsl|spv)$")
    message(FATAL_ERROR "ENGINE_SHADER_MODE must be one of auto, glsl, spv (got '${ENGINE_SHADER_MODE}').")
endif()

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
# Installed libraries and executables live side by side in bin/, so an
# $ORIGIN-relative install RPATH resolves them without LD_LIBRARY_PATH.
if (UNIX)
    set(CMAKE_INSTALL_RPATH "\$ORIGIN")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)
endif()

include(CTest)
# Supplies CMAKE_INSTALL_INCLUDEDIR/LIBDIR, used by the SDK layout.
include(GNUInstallDirs)

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

# Resolve the effective built-in shader form for the configuration being built.
# Baking needs the shader_baker helper, so disabling the tools falls back to GLSL.
if (ENGINE_SHADER_MODE STREQUAL "auto")
    set(ENGINE_SHADER_EFFECTIVE_MODE "$<IF:$<CONFIG:Debug>,glsl,spv>")
else()
    set(ENGINE_SHADER_EFFECTIVE_MODE "${ENGINE_SHADER_MODE}")
endif()
if (NOT ENGINE_BUILD_TOOLS)
    if (ENGINE_SHADER_MODE STREQUAL "spv")
        message(FATAL_ERROR
            "ENGINE_SHADER_MODE=spv requires ENGINE_BUILD_TOOLS=ON: shader_baker performs the baking.")
    elseif (ENGINE_SHADER_MODE STREQUAL "auto")
        message(WARNING "ENGINE_BUILD_TOOLS=OFF: shipping built-in shaders in GLSL form.")
        set(ENGINE_SHADER_EFFECTIVE_MODE "glsl")
    endif()
endif()

# Data-root staging area of the configuration being built. It holds the
# configuration's built-in assets in the form that will be installed, and is what
# the shader-form tests read. It deliberately does not live at the installed
# layout's executable-relative location (`<build>/bin/../share/needrename`),
# which would make build-tree executables resolve it as if it were installed.
set(ENGINE_DATA_STAGE_DIR "${CMAKE_BINARY_DIR}/stage/$<CONFIG>")

if (ENGINE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# --- Built-in asset staging ------------------------------------------------
# Defined before apps/examples/tests so their tests can depend on the staged
# content of the configuration being built.
file(GLOB_RECURSE ENGINE_BUILTIN_ASSET_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${BUILTIN_ASSETS_DIR}/*"
)
list(FILTER ENGINE_BUILTIN_ASSET_FILES EXCLUDE REGEX "\\.spv$")

if (TARGET shader_baker)
    set(ENGINE_SHADER_BAKER_ARGUMENT "-DSHADER_BAKER=$<TARGET_FILE:shader_baker>")
    set(ENGINE_SHADER_BAKER_DEPENDENCY shader_baker)
else()
    set(ENGINE_SHADER_BAKER_ARGUMENT)
    set(ENGINE_SHADER_BAKER_DEPENDENCY)
endif()

set(ENGINE_STAGED_BUILTIN_ASSETS_STAMP "${ENGINE_DATA_STAGE_DIR}/builtin_assets/.staged")
add_custom_command(
    OUTPUT "${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DSTAGE_ROOT=${ENGINE_DATA_STAGE_DIR}"
        "-DSOURCE_BUILTIN_ASSETS=${BUILTIN_ASSETS_DIR}"
        "-DSHADER_MODE=${ENGINE_SHADER_EFFECTIVE_MODE}"
        "${ENGINE_SHADER_BAKER_ARGUMENT}"
        "-DDEBUG_INFO=$<IF:$<CONFIG:Debug>,ON,OFF>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stage_builtin_assets.cmake"
    DEPENDS
        ${ENGINE_BUILTIN_ASSET_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stage_builtin_assets.cmake"
        ${ENGINE_SHADER_BAKER_DEPENDENCY}
    COMMENT "Staging built-in assets (shader mode: ${ENGINE_SHADER_EFFECTIVE_MODE})"
    VERBATIM
)

add_custom_target(builtin_assets_stage ALL DEPENDS "${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}")
set_target_properties(builtin_assets_stage PROPERTIES FOLDER build)

# The installed built-in assets are the staged content of the installed
# configuration: embedded-SPIR-V archives in spv mode, GLSL sources plus the
# include tree in glsl mode.
install(
    DIRECTORY "${ENGINE_DATA_STAGE_DIR}/builtin_assets/"
    DESTINATION share/needrename/builtin_assets
    COMPONENT runtime
    PATTERN ".staged" EXCLUDE
)

if (ENGINE_BUILD_APPS)
    # The Python bindings live in app/python and are configured with the other
    # apps, because they link the PhysicsApp target.
    add_subdirectory(app)
endif()
if (ENGINE_BUILD_PYTHON_BINDINGS AND NOT ENGINE_BUILD_APPS)
    message(
        WARNING
        "ENGINE_BUILD_PYTHON_BINDINGS requires ENGINE_BUILD_APPS (the bindings link PhysicsApp); "
        "the Python bindings will not be built"
    )
endif()
if (ENGINE_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()
if (ENGINE_BUILD_TESTS)
    add_subdirectory(test)
endif()

include(cmake/install_rules.cmake)
