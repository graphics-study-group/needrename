
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

# add_executable(vulkan_init_test vulkan_init_test.cpp)
# add_dependencies(vulkan_init_test shader)
# target_link_libraries(vulkan_init_test engine)
# add_test(NAME vulkan_init_test COMMAND vulkan_init_test)
# set_target_properties(vulkan_init_test PROPERTIES FOLDER tests)

# add_executable(constant_data_test constant_data_test.cpp)
# add_dependencies(constant_data_test shader)
# target_link_libraries(constant_data_test engine)
# add_test(NAME constant_data_test COMMAND constant_data_test)
# set_target_properties(constant_data_test PROPERTIES FOLDER tests)

add_executable(project_loading_test project_loading_test.cpp)
add_dependencies(project_loading_test shader)
target_link_libraries(project_loading_test engine)
add_test(NAME project_loading_test COMMAND project_loading_test)
set_target_properties(project_loading_test PROPERTIES FOLDER tests)

# add_executable(image_read_test image_read_test.cpp)
# add_dependencies(image_read_test shader)
# target_link_libraries(image_read_test engine)
# add_test(NAME image_read_test COMMAND image_read_test)
# set_target_properties(image_read_test PROPERTIES FOLDER tests)

add_executable(external_resource_loading_test external_resource_loading_test.cpp)
target_link_libraries(external_resource_loading_test engine)
add_test(NAME external_resource_loading_test COMMAND external_resource_loading_test)
set_target_properties(external_resource_loading_test PROPERTIES FOLDER tests)

add_executable(complex_mesh_test complex_mesh_test.cpp)
add_dependencies(complex_mesh_test shader)
target_link_libraries(complex_mesh_test engine)
add_test(NAME complex_mesh_test COMMAND complex_mesh_test 120)
set_target_properties(complex_mesh_test PROPERTIES FOLDER tests)

add_executable(shadow_map_test shadow_map_test.cpp)
add_dependencies(shadow_map_test shader)
target_link_libraries(shadow_map_test engine)
add_test(NAME shadow_map_test COMMAND shadow_map_test 120)
set_target_properties(shadow_map_test PROPERTIES FOLDER tests)

add_subdirectory(reflection_test)
add_subdirectory(serialization_test)

add_executable(imgui_test imgui_test.cpp)
target_link_libraries(imgui_test engine)
add_test(NAME imgui_test COMMAND imgui_test 120)
set_target_properties(imgui_test PROPERTIES FOLDER tests)

add_executable(new_material_test new_material_test.cpp)
add_dependencies(new_material_test shader)
target_link_libraries(new_material_test engine)
add_test(NAME new_material_test COMMAND new_material_test 120)
set_target_properties(new_material_test PROPERTIES FOLDER tests)

# Copy SDL dll
add_custom_command(TARGET SDL_init_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:SDL_init_test>
    COMMENT "Copying SDL DLLs"
)
