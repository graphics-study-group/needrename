include_guard(GLOBAL)

# Build a configuration-specific data root for staged-asset tests, browser
# preloading and installation. Physics adds its own kernels to this tree.
file(GLOB_RECURSE ENGINE_BUILTIN_ASSET_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${BUILTIN_ASSETS_DIR}/*"
)
list(FILTER ENGINE_BUILTIN_ASSET_FILES EXCLUDE REGEX "\\.spv$")

set(ENGINE_STAGED_BUILTIN_ASSETS_STAMP "${ENGINE_DATA_STAGE_DIR}/builtin_assets/.staged")
add_custom_command(
    OUTPUT "${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DSTAGE_ROOT=${ENGINE_DATA_STAGE_DIR}"
        "-DSOURCE_BUILTIN_ASSETS=${BUILTIN_ASSETS_DIR}"
        "-DSHADER_MODE=${ENGINE_SHADER_EFFECTIVE_MODE}"
        "-DSHADER_BAKER=${ENGINE_SHADER_BAKER_EXECUTABLE}"
        "-DBAKE_WGSL=${ENGINE_RHI_BACKEND_WEBGPU}"
        "-DWGSL_CONVERTER=${SHADER_WGSL_CONVERTER}"
        "-DDEBUG_INFO=$<IF:$<CONFIG:Debug>,ON,OFF>"
        -P "${CMAKE_SOURCE_DIR}/cmake/stage_builtin_assets.cmake"
    DEPENDS
        ${ENGINE_BUILTIN_ASSET_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/stage_builtin_assets.cmake"
        ${ENGINE_SHADER_BAKER_DEPENDENCY}
    COMMENT "Staging built-in assets (shader mode: ${ENGINE_SHADER_EFFECTIVE_MODE})"
    VERBATIM
)

add_custom_target(builtin_assets_stage ALL DEPENDS "${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}")
set_target_properties(builtin_assets_stage PROPERTIES FOLDER build)

# Browser pages depend on this aggregate to finish all data staging before link.
add_custom_target(runtime_data_stage ALL)
add_dependencies(runtime_data_stage builtin_assets_stage)
if (TARGET physics_shader_stage)
    add_dependencies(runtime_data_stage physics_shader_stage)
endif()
set_target_properties(runtime_data_stage PROPERTIES FOLDER build)

install(
    DIRECTORY "${ENGINE_DATA_STAGE_DIR}/builtin_assets/"
    DESTINATION share/needrename/builtin_assets
    COMPONENT runtime
    PATTERN ".staged" EXCLUDE
)
