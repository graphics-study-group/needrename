
# Do not rewrite the following commands into a single macro,
# which will disrupt VS Code integration with CTest.

add_executable(transform_test transform_test.cpp)
target_link_libraries(transform_test engine)
add_test(NAME transform_test COMMAND transform_test)
set_target_properties(transform_test PROPERTIES FOLDER tests)

add_executable(camera_transform_test camera_transform_test.cpp)
target_link_libraries(camera_transform_test engine)
add_test(NAME camera_transform_test COMMAND camera_transform_test)
set_target_properties(camera_transform_test PROPERTIES FOLDER tests)

add_executable(SDL_init_test SDL_init_test.cpp)
target_link_libraries(SDL_init_test engine)
add_test(NAME SDL_init_test COMMAND SDL_init_test)
set_target_properties(SDL_init_test PROPERTIES FOLDER tests)

add_executable(vulkan_init_test vulkan_init_test.cpp)
add_dependencies(vulkan_init_test shader)
target_link_libraries(vulkan_init_test engine)
add_test(NAME vulkan_init_test COMMAND vulkan_init_test)
set_target_properties(vulkan_init_test PROPERTIES FOLDER tests)

add_executable(constant_data_test constant_data_test.cpp)
add_dependencies(constant_data_test shader)
target_link_libraries(constant_data_test engine)
add_test(NAME constant_data_test COMMAND constant_data_test)
set_target_properties(constant_data_test PROPERTIES FOLDER tests)

add_executable(mesh_loading_test mesh_loading_test.cpp)
add_dependencies(mesh_loading_test shader)
target_link_libraries(mesh_loading_test engine)
add_test(NAME mesh_loading_test COMMAND mesh_loading_test)
set_target_properties(mesh_loading_test PROPERTIES FOLDER tests)

add_executable(image_read_test image_read_test.cpp)
add_dependencies(image_read_test shader)
target_link_libraries(image_read_test engine)
add_test(NAME image_read_test COMMAND image_read_test)
set_target_properties(image_read_test PROPERTIES FOLDER tests)

add_executable(external_resource_loading_test external_resource_loading_test.cpp)
target_link_libraries(external_resource_loading_test engine)
add_test(NAME external_resource_loading_test COMMAND external_resource_loading_test)
set_target_properties(external_resource_loading_test PROPERTIES FOLDER tests)

add_executable(complex_mesh_test complex_mesh_test.cpp)
add_dependencies(complex_mesh_test shader)
target_link_libraries(complex_mesh_test engine)
add_test(NAME complex_mesh_test COMMAND complex_mesh_test)
set_target_properties(complex_mesh_test PROPERTIES FOLDER tests)

include(${REFLECTION_PARSER_DIR}/parser.cmake)

add_executable(reflection_registeration_test reflection_registeration_test.cpp)
target_link_libraries(reflection_registeration_test PUBLIC engine)
target_include_directories(reflection_registeration_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/reflection_registeration_test)
set(all_include_dirs)
get_include_directories_for_target(reflection_registeration_test all_include_dirs)
add_reflection_parser(meta_reflection_registeration_test "${CMAKE_CURRENT_SOURCE_DIR}/reflection_registeration_test.h" "${CMAKE_CURRENT_SOURCE_DIR}/__generated__/reflection_registeration_test" "${all_include_dirs}")
add_dependencies(reflection_registeration_test meta_reflection_registeration_test)
target_link_libraries(reflection_registeration_test PUBLIC meta_reflection_registeration_test)
add_test(NAME reflection_registeration_test COMMAND reflection_registeration_test)
set_target_properties(reflection_registeration_test PROPERTIES FOLDER tests)

add_executable(serialization_test serialization_test.cpp)
target_link_libraries(serialization_test PUBLIC engine)
target_include_directories(serialization_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/serialization_test)
set(all_include_dirs)
get_include_directories_for_target(serialization_test all_include_dirs)
add_reflection_parser(meta_serialization_test "${CMAKE_CURRENT_SOURCE_DIR}/serialization_test.h" "${CMAKE_CURRENT_SOURCE_DIR}/__generated__/serialization_test" "${all_include_dirs}")
add_dependencies(serialization_test meta_serialization_test)
target_link_libraries(serialization_test PUBLIC meta_serialization_test)
add_test(NAME serialization_test COMMAND serialization_test)
set_target_properties(serialization_test PROPERTIES FOLDER tests)

# Copy SDL dll
add_custom_command(TARGET SDL_init_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:SDL_init_test>
    COMMENT "Copying SDL DLLs"
)
