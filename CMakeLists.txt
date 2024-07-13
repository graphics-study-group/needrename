cmake_minimum_required(VERSION 3.16)

project(hello_sdl3)
set(CMAKE_CXX_STANDARD 20)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(example)
