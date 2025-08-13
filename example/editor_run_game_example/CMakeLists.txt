include(${REFLECTION_PARSER_DIR}/parser.cmake)

get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp)
target_link_libraries(${ProjectId} PUBLIC editor engine)

# set(all_include_dirs)
# get_include_directories_for_target(${ProjectId} all_include_dirs)

# add_reflection_parser(meta_${ProjectId}  "${CMAKE_CURRENT_SOURCE_DIR}/CustomComponent.h" "${CMAKE_CURRENT_LIST_DIR}/__generated__" "${all_include_dirs}")
# add_dependencies(${ProjectId} meta_${ProjectId})
# target_link_libraries(${ProjectId} PRIVATE meta_${ProjectId})

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)

add_custom_command(TARGET ${ProjectId} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:${ProjectId}>
)
