project(stb)

add_library(stb STATIC stb_impl.cpp)
target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
