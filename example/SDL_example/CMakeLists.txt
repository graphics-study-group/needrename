get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp)
target_link_libraries(${ProjectId} PRIVATE SDL3::SDL3)

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)

