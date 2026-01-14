include(${REFLECTION_PARSER_DIR}/parser.cmake)

get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp)

# There seems to be some bugs hiding, as only PUBLIC dependencies are scanned
# by the reflection parser.
target_link_libraries(${ProjectId} PUBLIC editor engine)

set(all_include_dirs)
get_include_directories_for_target(${ProjectId} all_include_dirs)

add_reflection_parser(
    target_name meta_${ProjectId}
    reflection_search_files "${CMAKE_CURRENT_SOURCE_DIR}/CustomComponent.h"
    generated_code_dir "${CMAKE_CURRENT_LIST_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}"
    parent_projects meta_engine_generation
)
add_dependencies(${ProjectId} meta_${ProjectId})
target_link_libraries(${ProjectId} PRIVATE meta_${ProjectId})

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)

add_custom_command(TARGET ${ProjectId} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE:glslang>
    $<TARGET_FILE:engine>
    $<TARGET_FILE:editor>
    $<TARGET_FILE_DIR:${ProjectId}>
)
