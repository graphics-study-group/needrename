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

# ── Public-header compile check (task 3.6) ─────────────────────────────────
# A plain executable rather than anro_add_test, because it must NOT link the
# engine: the point is that the public Rhi headers compile without the Vulkan
# include directory (or any other backend header) on the path. Linking `Engine`
# would add exactly that directory and hide the very leak this guards against.
add_executable(rhi_public_headers_test rhi_public_headers_test.cpp)
target_link_libraries(rhi_public_headers_test PRIVATE EngineLibHeaderInterface AnnoRefl)
set_target_properties(rhi_public_headers_test PROPERTIES FOLDER tests/unit/rhi)
set_property(GLOBAL APPEND PROPERTY ENGINE_TEST_TARGETS rhi_public_headers_test)
add_test(NAME rhi_public_headers_test COMMAND rhi_public_headers_test)
set_tests_properties(rhi_public_headers_test PROPERTIES LABELS "headless")
