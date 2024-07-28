project(GLAD C)

add_library(GLAD STATIC src/glad.c)
target_include_directories(GLAD SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/")
