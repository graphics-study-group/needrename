cmake_minimum_required(VERSION 3.16)

project(hello_sdl3)
set(CMAKE_CXX_STANDARD 20)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

if (NOT(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    Message(FATAL_ERROR "This project supports GCC only.")
endif()

add_subdirectory(third_party)

if (RELEASE)
    add_compile_options(-O2)
else()
    add_compile_options(-g -Wall -Wextra -Wpedantic)
endif()
add_link_options(-static-libstdc++ -static-libgcc)

add_subdirectory(engine)
add_subdirectory(example)
