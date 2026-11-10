anro_add_test(asset_path_test FOLDER tests/unit/core)
anro_add_test(data_root_test FOLDER tests/unit/core)
# The same binary once more, started from the configuration's run directory
# instead of the assembled product: that is where a developer starts it, and it
# proves the build tree's own `share/needrename` is found by proximity too.
if (NOT EMSCRIPTEN)
    add_test(NAME data_root_build_tree
        COMMAND "${ENGINE_RUNTIME_DIR}/data_root_test${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(data_root_build_tree PROPERTIES
        LABELS "headless"
        ENVIRONMENT "NEEDRENAME_DATA_ROOT="
    )
endif()
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
