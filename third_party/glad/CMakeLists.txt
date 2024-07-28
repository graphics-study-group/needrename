add_library(GLAD STATIC src/glad.c)
target_include_directories(GLAD PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/")
