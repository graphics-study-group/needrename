file(GLOB_RECURSE ANROREFL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(AnnoRefl SHARED ${ANROREFL_SOURCE})
target_compile_definitions(AnnoRefl PRIVATE ANROREFL_DLL_EXPORTS)
target_include_directories(AnnoRefl
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/AnnoRefl
)
target_link_libraries(AnnoRefl
    PUBLIC
    glm
    json
)

set(ANROREFL_PARSER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/parser" CACHE STRING "Path to AnnoRefl parser directory")

enable_testing()
add_subdirectory(tests)
