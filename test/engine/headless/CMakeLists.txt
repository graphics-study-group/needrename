anro_add_test(headless_compute_test FOLDER tests/engine/headless)
anro_add_test(headless_offscreen_test FOLDER tests/engine/headless)
anro_add_test(compute_buffer_test FOLDER tests/engine/headless)
anro_add_test(new_rendergraph_test FOLDER tests/engine/headless)
anro_add_test(gpu_sum_by_key_test FOLDER tests/engine/headless)
anro_add_test(gpu_radix_primary_test FOLDER tests/engine/headless)
anro_add_test(xpbd_reduce_capacity_test FOLDER tests/engine/headless)

# Isolated timing for SumByKey's block reduction.  Built with the suite but
# disabled in CTest: it reports numbers instead of asserting, and it can be run
# by hand against a baseline shader.
anro_add_test(gpu_sum_by_key_bench FOLDER tests/engine/headless)
set_tests_properties(gpu_sum_by_key_bench PROPERTIES DISABLED TRUE)
