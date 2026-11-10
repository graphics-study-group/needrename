anro_add_test(project_loading_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(physics_registration_test WINDOWED TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(complex_mesh_test WINDOWED FRAMES 120 LINK "Engine;stb" TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(shadow_map_test WINDOWED FRAMES 300 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(skybox_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
# Dear ImGui draws through its own backend (task 10.4). Its WebGPU backend builds against the
# same standard webgpu.h the engine's WebGPU backend uses, so the same test drives either one -
# it is what verifies that the UI renders through the seam rather than only through Vulkan.
anro_add_test(imgui_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(new_material_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(input_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(compute_shader_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(pbr_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)
anro_add_test(mrt_test WINDOWED FRAMES 120 TIMEOUT 300 FOLDER tests/engine/windowed)

# The cross-backend check (task 9.2). The golden frame was produced by the Vulkan build from this
# same scene and camera; every backend is asked to reproduce it byte for byte, so a change that
# renders "roughly right" on one backend and differently on the other fails here.
#
# Both the test and its fixture come from the build tree: the fixture is the copy that travels with
# this configuration, so the comparison is against what the build staged.
add_test(
    NAME skybox_golden_test
    COMMAND
        ${CMAKE_COMMAND} -E env
            "SKYBOX_GOLDEN=${ENGINE_PRODUCT_DIR}/share/needrename/test_data/data/skybox_golden.rgba"
            "${ENGINE_PRODUCT_DIR}/bin/skybox_test${CMAKE_EXECUTABLE_SUFFIX}" 5
)
set_tests_properties(skybox_golden_test PROPERTIES
    LABELS "windowed"
    TIMEOUT 300
    ENVIRONMENT "NEEDRENAME_DATA_ROOT="
)
