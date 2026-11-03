# Headless tests that open a device and record through it directly. Those sources name
# Vulkan types today (a Vulkan device fixture, `vk::CommandBuffer` for a one-time
# submission), so they belong to the Vulkan backend and are not registered for another
# one. The portable ones below are registered for every backend.
if (ENGINE_RHI_BACKEND_VULKAN)
    anro_add_test(headless_compute_test FOLDER tests/engine/headless)
    anro_add_test(headless_offscreen_test FOLDER tests/engine/headless)
    anro_add_test(compute_buffer_test FOLDER tests/engine/headless)
    anro_add_test(gpu_sum_by_key_test FOLDER tests/engine/headless)
    anro_add_test(gpu_radix_multiblock_test FOLDER tests/engine/headless)
    anro_add_test(gpu_compact_unique_test FOLDER tests/engine/headless)
    anro_add_test(xpbd_reduce_capacity_test FOLDER tests/engine/headless)
    anro_add_test(model_matrices_ownership_test FOLDER tests/engine/headless)
    anro_add_test(model_matrices_initialization_test FOLDER tests/engine/headless)
    anro_add_test(gpu_buffer_retirement_test FOLDER tests/engine/headless)
    anro_add_test(gpu_resource_identity_test FOLDER tests/engine/headless)

    # Isolated timing for SumByKey's block reduction.  Built with the suite but
    # disabled in CTest: it reports numbers instead of asserting, and it can be run
    # by hand against a baseline shader.
    anro_add_test(gpu_sum_by_key_bench FOLDER tests/engine/headless)
    set_tests_properties(gpu_sum_by_key_bench PROPERTIES DISABLED TRUE)

    # This test is too slow
    anro_add_test(xpbd_contact_friction_test FOLDER tests/engine/headless)
    set_tests_properties(xpbd_contact_friction_test PROPERTIES DISABLED TRUE)
endif()

# Portable: it names only the seam's types, so it registers for every backend.
anro_add_test(host_write_window_test FOLDER tests/engine/headless)


anro_add_test(new_rendergraph_test FOLDER tests/engine/headless)

# Uses a hand-opened Vulkan command buffer to drive the physics façade, so it is a Vulkan test.
if (ENGINE_RHI_BACKEND_VULKAN)
    anro_add_test(physics_system_model_matrices_test LINK "Engine;EnginePhysics" EXTRA_LABELS gpu FOLDER tests/engine/headless)
endif()

# Task 8.1: the desktop WebGPU target creates a device and reports its limits. Built only
# for that backend, because on Vulkan the rest of this directory already exercises the seam.
if (ENGINE_RHI_BACKEND_WEBGPU)
    anro_add_test(webgpu_device_test LINK "EngineRhi" FOLDER tests/engine/headless)
endif()
