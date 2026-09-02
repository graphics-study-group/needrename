project(tinyxml2)

add_library(tinyxml2 STATIC tinyxml2.cpp)
set_target_properties(tinyxml2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(tinyxml2 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
