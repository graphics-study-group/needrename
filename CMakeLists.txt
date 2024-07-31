cmake_minimum_required(VERSION 3.16)

project(hello_sdl3)
set(CMAKE_CXX_STANDARD 20)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

if (NOT(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    Message(FATAL_ERROR "This project supports GCC only.")
endif()

add_subdirectory(third_party)

# Debug options
add_compile_options($<$<CONFIG:Debug>:-g>)
add_compile_options($<$<CONFIG:Debug>:-Og>)
add_compile_options($<$<CONFIG:Debug>:-Wall>)
add_compile_options($<$<CONFIG:Debug>:-Wextra>)
add_compile_options($<$<CONFIG:Debug>:-Wpedantic>)
add_compile_options($<$<CONFIG:Debug>:-Weffc++>)
# add_compile_options($<$<CONFIG:Debug>:-pg>)
# add_link_options($<$<CONFIG:Debug>:-pg>)

# Link libstdc++, etc. statically
add_link_options(-static-libstdc++ -static-libgcc)

add_subdirectory(engine)
add_subdirectory(example)

include(CTest)
add_subdirectory(test)
