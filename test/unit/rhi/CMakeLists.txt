anro_add_test(rhi_standalone_test EXTRA_LABELS gpu FOLDER tests/unit/rhi)
anro_add_test(submission_helper_test EXTRA_LABELS gpu FOLDER tests/unit/rhi)
anro_add_test(epoch_tracker_test EXTRA_LABELS gpu FOLDER tests/unit/rhi)
anro_add_test(descriptor_arena_test EXTRA_LABELS gpu FOLDER tests/unit/rhi)
anro_add_test(buffer_reallocation_test EXTRA_LABELS gpu FOLDER tests/unit/rhi)
anro_add_test(compute_kernel_test EXTRA_LABELS gpu FOLDER tests/unit/rhi)

# The debug assertion for an oversized push-constant value aborts the process,
# so the case is expected to fail: WILL_FAIL inverts that into a pass.
add_test(NAME compute_kernel_oversize_test COMMAND compute_kernel_test --oversize-push)
set_tests_properties(compute_kernel_oversize_test PROPERTIES LABELS "headless;gpu" WILL_FAIL TRUE)
