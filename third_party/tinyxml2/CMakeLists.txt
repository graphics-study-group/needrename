project(tinyxml2)

add_library(tinyxml2 STATIC tinyxml2.cpp)
target_include_directories(tinyxml2 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
