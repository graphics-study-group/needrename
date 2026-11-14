anro_add_test(cascade_fit_test FOLDER tests/unit/render)
anro_add_test(shadow_constants_test FOLDER tests/unit/render)
# The constants this test checks are declared in hand-written GLSL headers. An spv-mode staging
# tree does not ship them (it ships baked archives), so the test is told where the sources are.
target_compile_definitions(shadow_constants_test PRIVATE NEEDRENAME_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
anro_add_test(mesh_asset_legacy_test FOLDER tests/unit/render)
# The present-mode policy is the WebGPU backend's, and its header names the standard interface's
# present-mode enumeration; other backends do not compile the unit at all.
if (ENGINE_RHI_BACKEND_WEBGPU)
    anro_add_test(present_mode_policy_test FOLDER tests/unit/render)
endif()
