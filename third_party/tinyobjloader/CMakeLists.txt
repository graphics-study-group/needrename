project(tinyobjloader)

add_library(tinyobjloader STATIC tiny_obj_loader.cc)
target_include_directories(tinyobjloader SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
