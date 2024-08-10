project(json)

add_library(json STATIC json.cpp)
target_include_directories(json SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
