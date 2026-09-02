project(tinyobjloader)

add_library(tinyobjloader STATIC tiny_obj_loader.cc)
set_target_properties(tinyobjloader PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(tinyobjloader SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
