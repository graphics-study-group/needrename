project(cereal)

add_library(cereal STATIC placeholder.cpp)
target_include_directories(cereal SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
