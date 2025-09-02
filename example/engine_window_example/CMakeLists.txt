get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp)
target_link_libraries(${ProjectId} PRIVATE SDL3::SDL3 engine)

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)

add_custom_command(TARGET ${ProjectId} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE:engine>
    $<TARGET_FILE_DIR:${ProjectId}>
)
