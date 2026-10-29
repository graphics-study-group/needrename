anro_add_test(project_loading_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(physics_registration_test WINDOWED TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(complex_mesh_test WINDOWED FRAMES 120 LINK "Engine;stb" TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(shadow_map_test WINDOWED FRAMES 300 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(skybox_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(imgui_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(new_material_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(input_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(compute_shader_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(pbr_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(mrt_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)

# Same renderer, but against the built-in assets staged for this configuration:
# proves that the staged archive form (embedded SPIR-V in spv mode) loads and
# renders without touching the developer source tree.
add_test(
    NAME mrt_test_staged_builtin_assets
    COMMAND
        ${CMAKE_COMMAND} -E env "NEEDRENAME_DATA_ROOT=${ENGINE_DATA_STAGE_DIR}"
        $<TARGET_FILE:mrt_test> 5
)
set_tests_properties(mrt_test_staged_builtin_assets PROPERTIES LABELS "windowed" TIMEOUT 300)
add_dependencies(mrt_test builtin_assets_stage)
