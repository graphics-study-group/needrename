anro_add_test(asset_path_test FOLDER tests/unit/core)
anro_add_test(data_root_test FOLDER tests/unit/core)
anro_add_test(transform_test FOLDER tests/unit/core)
anro_add_test(camera_transform_test FOLDER tests/unit/core)
anro_add_test(delegate_test FOLDER tests/unit/core)
anro_add_test(flag_bits_test FOLDER tests/unit/core)
anro_add_test(vertex_attribute_test FOLDER tests/unit/core)
# structured_buffer_test compiles a small GLSL module with glslang and reflects it, which is
# the Vulkan backend's runtime path; a WebGPU build has the bake instead of the compiler.
if (ENGINE_RHI_BACKEND_VULKAN)
    anro_add_test(structured_buffer_test FOLDER tests/unit/core)
endif()
anro_add_test(SDL_init_test FOLDER tests/unit/core)
