anro_add_test(headless_compute_test FOLDER tests/engine/headless)
anro_add_test(headless_offscreen_test FOLDER tests/engine/headless)
anro_add_test(compute_buffer_test FOLDER tests/engine/headless)
anro_add_test(new_rendergraph_test FOLDER tests/engine/headless)
anro_add_test(gpu_sum_by_key_test FOLDER tests/engine/headless)
anro_add_test(gpu_radix_multiblock_test FOLDER tests/engine/headless)
anro_add_test(gpu_compact_unique_test FOLDER tests/engine/headless)
anro_add_test(xpbd_reduce_capacity_test FOLDER tests/engine/headless)
anro_add_test(model_matrices_ownership_test FOLDER tests/engine/headless)
anro_add_test(gpu_buffer_retirement_test FOLDER tests/engine/headless)
anro_add_test(physics_system_model_matrices_test LINK "Engine;EnginePhysics" EXTRA_LABELS gpu FOLDER tests/engine/headless)

# gpu_compact_unique_test is the one test that loads a physics compute shader
# itself (the broad phase's unpack_pairs.comp, which belongs to no
# gpu_algorithm module), so it resolves ENGINE_PHYSICS_SPIRV_DIR through the same
# per-config subdirectory EnginePhysics compiles against.  Without this the macro
# falls back to the bare SPIR-V root, which holds no shader.
target_compile_definitions(gpu_compact_unique_test PRIVATE
    ENGINE_PHYSICS_SPIRV_CONFIG_SUBDIR="$<IF:$<CONFIG:Debug>,debug,release>"
)

# Isolated timing for SumByKey's block reduction.  Built with the suite but
# disabled in CTest: it reports numbers instead of asserting, and it can be run
# by hand against a baseline shader.
anro_add_test(gpu_sum_by_key_bench FOLDER tests/engine/headless)
set_tests_properties(gpu_sum_by_key_bench PROPERTIES DISABLED TRUE)
