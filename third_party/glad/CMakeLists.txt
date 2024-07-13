Set(GLAD_INCLUDE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/)

add_library(GLAD STATIC src/glad.c)
target_include_directories(GLAD PUBLIC ${GLAD_INCLUDE_DIRECTORY})
