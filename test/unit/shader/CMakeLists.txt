# These tests compile GLSL at runtime through glslang and reflect the SPIR-V, both of which
# belong to the Vulkan backend: a WebGPU build has neither library, because the bake produced
# the same facts ahead of time.
if (ENGINE_RHI_BACKEND_VULKAN)
    anro_add_test(shader_refl_test FOLDER tests/unit/shader)
    anro_add_test(builtin_shader_compile_test FOLDER tests/unit/shader)
    add_subdirectory(shader_compile_test)
    # These two compile built-in GLSL sources, which exist only in the source
    # tree: a staged tree that ships baked archives (spv mode, and every WebGPU
    # build) deliberately drops them. They read the sources directly rather than
    # going through the resolved data root for that reason.
    target_compile_definitions(shader_refl_test PRIVATE NEEDRENAME_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(builtin_shader_compile_test PRIVATE NEEDRENAME_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# Verifies that the built-in assets staged for this configuration carry the
# selected shader form (embedded SPIR-V vs GLSL sources plus include tree).
anro_add_test(
    shader_archive_form_test
    FOLDER tests/unit/shader
    ARGS "${ENGINE_DATA_STAGE_DIR}" "${ENGINE_SHADER_EFFECTIVE_MODE}"
)
add_dependencies(shader_archive_form_test builtin_assets_stage)
