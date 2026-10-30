anro_add_test(shader_refl_test FOLDER tests/unit/shader)
anro_add_test(builtin_shader_compile_test FOLDER tests/unit/shader)
add_subdirectory(shader_compile_test)

# Verifies that the built-in assets staged for this configuration carry the
# selected shader form (embedded SPIR-V vs GLSL sources plus include tree).
anro_add_test(
    shader_archive_form_test
    FOLDER tests/unit/shader
    ARGS "${ENGINE_DATA_STAGE_DIR}" "${ENGINE_SHADER_EFFECTIVE_MODE}"
)
add_dependencies(shader_archive_form_test builtin_assets_stage)
