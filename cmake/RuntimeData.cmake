include_guard(GLOBAL)

# Runtime data staging for one build configuration.
#
# The data root this build produces is `share/needrename` in the configuration's
# prefix (`output/<config>`), one level above the `bin/` the linker writes into, so
# the programs find it by the same rule an installed one uses - no environment
# variable, no build-machine path. It is built category by category, and every
# category is its own command with its own stamp and its own source-file
# dependencies, because the categories change at very different rates and one of
# them is expensive: editing a sample asset must not re-run the shader bake, and a
# rebuild with nothing changed must run nothing at all.
#
#   builtin_assets/   the built-in asset database in the configuration's shader form
#                     (baked by the shader baker, WGSL included for WebGPU)
#   assets/           sample content
#   projects/         project templates
#   test_data/        test fixtures
#   spirv/            physics modules, staged by engine/Physics
#
# The build tree is the prefix here, so nothing has to be assembled for a run; the
# staging commands are also the only source of the data an install copies.

set(ENGINE_STAGE_DIR "${ENGINE_DATA_STAGE_DIR}")
set(ENGINE_STAGE_STAMPS_DIR "${ENGINE_STAGE_DIR}/.stamps")

# --- Built-in assets: copy, then bake the shaders ---------------------------
file(GLOB_RECURSE ENGINE_BUILTIN_ASSET_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${BUILTIN_ASSETS_DIR}/*"
)
list(FILTER ENGINE_BUILTIN_ASSET_FILES EXCLUDE REGEX "\\.spv$")

set(ENGINE_STAGED_BUILTIN_ASSETS_STAMP "${ENGINE_STAGE_STAMPS_DIR}/builtin_assets")
add_custom_command(
    OUTPUT "${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DSTAGE_ROOT=${ENGINE_STAGE_DIR}"
        "-DSOURCE_BUILTIN_ASSETS=${BUILTIN_ASSETS_DIR}"
        "-DSHADER_MODE=${ENGINE_SHADER_EFFECTIVE_MODE}"
        "-DSHADER_BAKER=${ENGINE_SHADER_BAKER_EXECUTABLE}"
        "-DBAKE_WGSL=${ENGINE_RHI_BACKEND_WEBGPU}"
        "-DWGSL_CONVERTER=${SHADER_WGSL_CONVERTER}"
        "-DDEBUG_INFO=$<IF:$<CONFIG:Debug>,ON,OFF>"
        "-DSTAMP=${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}"
        -P "${CMAKE_SOURCE_DIR}/cmake/stage_builtin_assets.cmake"
    DEPENDS
        ${ENGINE_BUILTIN_ASSET_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/stage_builtin_assets.cmake"
        ${ENGINE_SHADER_BAKER_DEPENDENCY}
    COMMENT "Staging built-in assets (shader mode: ${ENGINE_SHADER_EFFECTIVE_MODE})"
    VERBATIM
)
add_custom_target(builtin_assets_stage ALL DEPENDS "${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}")
set_target_properties(builtin_assets_stage PROPERTIES
    FOLDER build
    ADDITIONAL_CLEAN_FILES "${ENGINE_STAGE_DIR}/builtin_assets"
)

# --- Sample content and project templates -----------------------------------
# Two commands rather than one: they copy different trees, so they are restaged
# independently. Documentation screenshots (`assets/img`) never enter a product.
file(GLOB_RECURSE ENGINE_ASSET_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${ASSETS_DIR}/*"
)
file(GLOB_RECURSE ENGINE_PROJECT_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${PROJECTS_DIR}/*"
)

set(ENGINE_STAGED_ASSETS_STAMP "${ENGINE_STAGE_STAMPS_DIR}/assets")
add_custom_command(
    OUTPUT "${ENGINE_STAGED_ASSETS_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE=${ASSETS_DIR}"
        "-DDESTINATION=${ENGINE_STAGE_DIR}/assets"
        "-DEXCLUDES=img;CMakeLists.txt"
        "-DSTAMP=${ENGINE_STAGED_ASSETS_STAMP}"
        -P "${CMAKE_SOURCE_DIR}/cmake/stage_tree.cmake"
    DEPENDS
        ${ENGINE_ASSET_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/stage_tree.cmake"
    COMMENT "Staging sample content"
    VERBATIM
)

set(ENGINE_STAGED_PROJECTS_STAMP "${ENGINE_STAGE_STAMPS_DIR}/projects")
add_custom_command(
    OUTPUT "${ENGINE_STAGED_PROJECTS_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE=${PROJECTS_DIR}"
        "-DDESTINATION=${ENGINE_STAGE_DIR}/projects"
        "-DEXCLUDES=CMakeLists.txt"
        "-DSTAMP=${ENGINE_STAGED_PROJECTS_STAMP}"
        -P "${CMAKE_SOURCE_DIR}/cmake/stage_tree.cmake"
    DEPENDS
        ${ENGINE_PROJECT_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/stage_tree.cmake"
    COMMENT "Staging project templates"
    VERBATIM
)

add_custom_target(content_stage ALL
    DEPENDS "${ENGINE_STAGED_ASSETS_STAMP}" "${ENGINE_STAGED_PROJECTS_STAMP}"
)
set_target_properties(content_stage PROPERTIES
    FOLDER build
    ADDITIONAL_CLEAN_FILES "${ENGINE_STAGE_DIR}/assets;${ENGINE_STAGE_DIR}/projects"
)

# --- Test fixtures ----------------------------------------------------------
file(GLOB_RECURSE ENGINE_TEST_DATA_FILES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${TESTS_DIR}/*"
)

set(ENGINE_STAGED_TEST_DATA_STAMP "${ENGINE_STAGE_STAMPS_DIR}/test_data")
add_custom_command(
    OUTPUT "${ENGINE_STAGED_TEST_DATA_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE=${TESTS_DIR}"
        "-DDESTINATION=${ENGINE_STAGE_DIR}/test_data"
        "-DEXCLUDES=CMakeLists.txt;__generated__"
        "-DSTAMP=${ENGINE_STAGED_TEST_DATA_STAMP}"
        -P "${CMAKE_SOURCE_DIR}/cmake/stage_tree.cmake"
    DEPENDS
        ${ENGINE_TEST_DATA_FILES}
        "${CMAKE_SOURCE_DIR}/cmake/stage_tree.cmake"
    COMMENT "Staging test fixtures"
    VERBATIM
)
add_custom_target(test_data_stage ALL DEPENDS "${ENGINE_STAGED_TEST_DATA_STAMP}")
set_target_properties(test_data_stage PROPERTIES
    FOLDER build
    ADDITIONAL_CLEAN_FILES "${ENGINE_STAGE_DIR}/test_data"
)

# --- Aggregate --------------------------------------------------------------
# Everything a run needs from the data root. Physics adds its own category to
# this list, so one dependency covers the whole tree.
add_custom_target(runtime_data_stage ALL)
add_dependencies(runtime_data_stage builtin_assets_stage content_stage test_data_stage)
if (TARGET physics_shader_stage)
    add_dependencies(runtime_data_stage physics_shader_stage)
endif()
set_target_properties(runtime_data_stage PROPERTIES FOLDER build)

# The category stamps, for consumers that need file-level dependencies rather
# than a phony target: the Python package's assembly is out of date exactly when
# one of these is, so depending on them keeps a no-op build free of recopying.
set(ENGINE_STAGED_DATA_STAMPS
    "${ENGINE_STAGED_BUILTIN_ASSETS_STAMP}"
    "${ENGINE_STAGED_ASSETS_STAMP}"
    "${ENGINE_STAGED_PROJECTS_STAMP}"
    "${ENGINE_STAGED_TEST_DATA_STAMP}"
)
if (DEFINED PHYSICS_STAGED_STAMP)
    list(APPEND ENGINE_STAGED_DATA_STAMPS "${PHYSICS_STAGED_STAMP}")
endif()

# The data components are installed from the staging tree, never from the
# checkout: install copies what the build produced, including the baked shaders.
install(
    DIRECTORY "${ENGINE_STAGE_DIR}/builtin_assets/"
    DESTINATION share/needrename/builtin_assets
    COMPONENT runtime
)
