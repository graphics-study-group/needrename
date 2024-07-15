Set(TINYOBJLOADER_INCLUDE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/)

add_library(tinyobjloader STATIC tiny_obj_loader.cc)
target_include_directories(tinyobjloader PUBLIC ${TINYOBJLOADER_INCLUDE_DIRECTORY})
