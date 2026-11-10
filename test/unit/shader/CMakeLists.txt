# These tests compile GLSL at runtime through glslang and reflect the SPIR-V, both of which
# belong to the Vulkan backend: a WebGPU build has neither library, because the bake produced
# the same facts ahead of time.
if (ENGINE_RHI_BACKEND_VULKAN)
    anro_add_test(shader_refl_test FOLDER tests/unit/shader)
    anro_add_test(builtin_shader_compile_test FOLDER tests/unit/shader)
    add_subdirectory(shader_compile_test)
    # These two compile built-in GLSL sources, which exist only in the source
    # tree: a staged data root that carries baked archives (spv mode, and every
    # WebGPU build) deliberately drops them. They read the sources directly rather
    # than through the data root for that reason.
    target_compile_definitions(shader_refl_test PRIVATE
        NEEDRENAME_SOURCE_BUILTIN_ASSETS="${BUILTIN_ASSETS_DIR}")
    target_compile_definitions(builtin_shader_compile_test PRIVATE
        NEEDRENAME_SOURCE_BUILTIN_ASSETS="${BUILTIN_ASSETS_DIR}")
endif()

# Verifies that the staged data root carries the built-in shaders in the form
# this configuration selected (embedded SPIR-V vs GLSL sources plus include tree),
# resolved the way a run resolves them - from beside the executable.
anro_add_test(
    shader_archive_form_test
    FOLDER tests/unit/shader
    ARGS "${ENGINE_SHADER_EFFECTIVE_MODE}"
)
