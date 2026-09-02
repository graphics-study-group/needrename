project(stb)

add_library(stb STATIC stb_impl.cpp stb_image_write_impl.cpp)
set_target_properties(stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
