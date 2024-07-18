Set(STB_INCLUDE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/)

add_library(stb STATIC stb_impl.cpp)
target_include_directories(stb PUBLIC ${STB_INCLUDE_DIRECTORY})
