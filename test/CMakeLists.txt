
# Do not rewrite the following commands into a single macro,
# which will disrupt VS Code integration with CTest.

add_executable(SDL_init_test SDL_init_test.cpp)
target_link_libraries(SDL_init_test Engine)
add_test(NAME SDL_init_test COMMAND SDL_init_test)
set_target_properties(SDL_init_test PROPERTIES FOLDER tests)

add_executable(asset_path_test asset_path_test.cpp)
target_link_libraries(asset_path_test Engine)
add_test(NAME asset_path_test COMMAND asset_path_test)
set_target_properties(asset_path_test PROPERTIES FOLDER tests)

# add_executable(vulkan_init_test vulkan_init_test.cpp)
# target_link_libraries(vulkan_init_test Engine)
# add_test(NAME vulkan_init_test COMMAND vulkan_init_test)
# set_target_properties(vulkan_init_test PROPERTIES FOLDER tests)

# add_executable(constant_data_test constant_data_test.cpp)
# target_link_libraries(constant_data_test Engine)
# add_test(NAME constant_data_test COMMAND constant_data_test)
# set_target_properties(constant_data_test PROPERTIES FOLDER tests)

add_executable(project_loading_test project_loading_test.cpp)
target_link_libraries(project_loading_test Engine)
add_test(NAME project_loading_test COMMAND project_loading_test 120)
set_target_properties(project_loading_test PROPERTIES FOLDER tests)

add_executable(complex_mesh_test complex_mesh_test.cpp)
target_link_libraries(complex_mesh_test Engine stb)
add_test(NAME complex_mesh_test COMMAND complex_mesh_test 120)
set_target_properties(complex_mesh_test PROPERTIES FOLDER tests)

add_executable(shadow_map_test shadow_map_test.cpp)
target_link_libraries(shadow_map_test Engine)
add_test(NAME shadow_map_test COMMAND shadow_map_test 120)
set_target_properties(shadow_map_test PROPERTIES FOLDER tests)

add_executable(skybox_test skybox_test.cpp)
target_link_libraries(skybox_test Engine)
add_test(NAME skybox_test COMMAND skybox_test 120)
set_target_properties(skybox_test PROPERTIES FOLDER tests)

add_executable(imgui_test imgui_test.cpp)
target_link_libraries(imgui_test Engine)
add_test(NAME imgui_test COMMAND imgui_test 120)
set_target_properties(imgui_test PROPERTIES FOLDER tests)

add_executable(new_material_test new_material_test.cpp)
target_link_libraries(new_material_test Engine)
add_test(NAME new_material_test COMMAND new_material_test 120)
set_target_properties(new_material_test PROPERTIES FOLDER tests)

add_executable(input_test input_test.cpp)
target_link_libraries(input_test Engine)
add_test(NAME input_test COMMAND input_test 120)
set_target_properties(input_test PROPERTIES FOLDER tests)

add_executable(compute_shader_test compute_shader_test.cpp)
target_link_libraries(compute_shader_test Engine)
add_test(NAME compute_shader_test COMMAND compute_shader_test 120)
set_target_properties(compute_shader_test PROPERTIES FOLDER tests)

add_executable(compute_buffer_test compute_buffer_test.cpp)
target_link_libraries(compute_buffer_test Engine)
add_test(NAME compute_buffer_test COMMAND compute_buffer_test)
set_target_properties(compute_buffer_test PROPERTIES FOLDER tests)

add_executable(pbr_test pbr_test.cpp)
target_link_libraries(pbr_test Engine)
add_test(NAME pbr_test COMMAND pbr_test 120)
set_target_properties(pbr_test PROPERTIES FOLDER tests)

add_executable(mrt_test mrt_test.cpp)
target_link_libraries(mrt_test Engine)
add_test(NAME mrt_test COMMAND mrt_test 120)
set_target_properties(mrt_test PROPERTIES FOLDER tests)

add_executable(physics_registration_test physics_registration_test.cpp)
target_link_libraries(physics_registration_test Engine)
add_test(NAME physics_registration_test COMMAND physics_registration_test)
set_target_properties(physics_registration_test PROPERTIES FOLDER tests)

add_executable(headless_compute_test headless_compute_test.cpp)
target_link_libraries(headless_compute_test Engine)
add_test(NAME headless_compute_test COMMAND headless_compute_test)
set_target_properties(headless_compute_test PROPERTIES FOLDER tests)

add_executable(rhi_standalone_test rhi_standalone_test.cpp)
target_link_libraries(rhi_standalone_test Engine)
add_test(NAME rhi_standalone_test COMMAND rhi_standalone_test)
set_target_properties(rhi_standalone_test PROPERTIES FOLDER tests)

add_executable(headless_offscreen_test headless_offscreen_test.cpp)
target_link_libraries(headless_offscreen_test Engine)
add_test(NAME headless_offscreen_test COMMAND headless_offscreen_test)
set_target_properties(headless_offscreen_test PROPERTIES FOLDER tests)

add_executable(submission_helper_test submission_helper_test.cpp)
target_link_libraries(submission_helper_test Engine)
add_test(NAME submission_helper_test COMMAND submission_helper_test)
set_target_properties(submission_helper_test PROPERTIES FOLDER tests)

add_executable(physics_app_headless_test physics_app_headless_test.cpp)
target_link_libraries(physics_app_headless_test Engine PhysicsApp)
add_test(NAME physics_app_headless_test COMMAND physics_app_headless_test)
set_target_properties(physics_app_headless_test PROPERTIES FOLDER tests)

add_executable(physics_app_offscreen_test physics_app_offscreen_test.cpp)
target_link_libraries(physics_app_offscreen_test Engine PhysicsApp)
add_test(NAME physics_app_offscreen_test COMMAND physics_app_offscreen_test)
set_target_properties(physics_app_offscreen_test PROPERTIES FOLDER tests)

add_executable(physics_app_windowed_test physics_app_windowed_test.cpp)
target_link_libraries(physics_app_windowed_test Engine PhysicsApp)
add_test(NAME physics_app_windowed_test COMMAND physics_app_windowed_test 120)
set_target_properties(physics_app_windowed_test PROPERTIES FOLDER tests)

