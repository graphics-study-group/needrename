# A standalone project that consumes the *installed* engine SDK.
#
# It is deliberately NOT part of the engine build: nothing in the repository's
# CMakeLists.txt adds this directory, and every path it uses resolves through an
# installed prefix. If a build of this project succeeds, that prefix is
# self-sufficient for consumers.
#
# See README.md for how to build it by hand.

cmake_minimum_required(VERSION 3.21)
project(needrename_sdk_consumer CXX)
set(CMAKE_CXX_STANDARD 20)

# Default to the checkout that contains this example, so the documented commands
# need no paths at all. Pass -DCMAKE_PREFIX_PATH=<prefix> to consume a different
# installation.
get_filename_component(_engine_checkout "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Interpreter for the reflection parser: prefer the checkout's virtual
# environment when it exists. The layout is per-platform on purpose — a checkout
# shared between Windows and Linux also holds a Windows interpreter under
# .venv/Scripts, which must not be picked when parsing on Linux.
if (NOT Python3_EXECUTABLE)
    if (WIN32)
        set(_python_candidate ".venv/Scripts/python.exe")
    else()
        set(_python_candidate ".venv/bin/python")
    endif()
    if (EXISTS "${_engine_checkout}/${_python_candidate}")
        set(Python3_EXECUTABLE "${_engine_checkout}/${_python_candidate}")
    endif()
endif()

# Resolves the exported `needrename::` targets from an installed prefix.
find_package(needrename CONFIG QUIET)
if (NOT needrename_FOUND)
    find_package(needrename CONFIG REQUIRED PATHS "${_engine_checkout}/install")
endif()

# 1. Links the engine and exercises the runtime data locator: it must find the
#    installed data root and the built-in assets through the executable's
#    location, with no engine source directory and no environment variable.
add_executable(sdk_consumer_minimal src/minimal_main.cpp)
target_link_libraries(sdk_consumer_minimal PRIVATE needrename::Engine)

# 2. Declares a component that derives from an engine type and asks the shipped
#    parser to generate registration/serialization code for it, then round-trips
#    it through the archive format. This is the same code path the in-tree
#    examples use for their custom components.
add_executable(sdk_consumer_reflection src/reflection_main.cpp)
target_link_libraries(sdk_consumer_reflection PRIVATE needrename::Engine needrename::json)

needrename_add_reflection(sdk_consumer_reflection
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/external_component.h")

# Both programs must find the prefix's shared libraries at run time. Windows
# resolves them from PATH; on Linux the build RPATH points at the directory the
# exported module target reports, so no LD_LIBRARY_PATH is needed.
if (UNIX)
    set_target_properties(sdk_consumer_minimal sdk_consumer_reflection PROPERTIES
        BUILD_RPATH "$<TARGET_FILE_DIR:needrename::EngineCore>")
endif()
