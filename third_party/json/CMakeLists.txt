project(json)

add_library(json STATIC json.cpp)
set_target_properties(json PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(json SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
