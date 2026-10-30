# Stage the built-in asset database for one build configuration.
#
# Invoked by the `builtin_assets_stage` target with:
#   -DSTAGE_ROOT=<data-root staging directory>
#   -DSOURCE_BUILTIN_ASSETS=<source builtin_assets directory>
#   -DSHADER_MODE=<glsl|spv>
#   -DSHADER_BAKER=<shader_baker executable>
#   -DDEBUG_INFO=<ON|OFF>
#
# glsl mode copies the built-in assets as-is: the shader archives stay in GLSL
# form and the shader include tree ships with them.
# spv mode copies them, bakes every shader archive to embedded SPIR-V form, then
# drops the GLSL sources and the include tree — they are not needed to load a
# baked archive.

if (NOT STAGE_ROOT)
    message(FATAL_ERROR "stage_builtin_assets: STAGE_ROOT is not set.")
endif()
if (NOT SOURCE_BUILTIN_ASSETS)
    message(FATAL_ERROR "stage_builtin_assets: SOURCE_BUILTIN_ASSETS is not set.")
endif()
if (NOT SHADER_MODE)
    message(FATAL_ERROR "stage_builtin_assets: SHADER_MODE is not set.")
endif()

set(staged_builtin_assets "${STAGE_ROOT}/builtin_assets")

message(STATUS "Staging built-in assets (shader mode: ${SHADER_MODE}) -> ${staged_builtin_assets}")

file(REMOVE_RECURSE "${staged_builtin_assets}")
file(COPY "${SOURCE_BUILTIN_ASSETS}/" DESTINATION "${staged_builtin_assets}" PATTERN ".gitignore" EXCLUDE)

if (SHADER_MODE STREQUAL "glsl")
    message(STATUS "Staged built-in shaders in GLSL form.")
elseif (SHADER_MODE STREQUAL "spv")
    if (NOT SHADER_BAKER OR NOT EXISTS "${SHADER_BAKER}")
        message(FATAL_ERROR "stage_builtin_assets: shader_baker executable not found ('${SHADER_BAKER}').")
    endif()

    set(baker_arguments "${staged_builtin_assets}/shaders")
    if (DEBUG_INFO)
        list(APPEND baker_arguments --debug-info)
    endif()

    execute_process(
        COMMAND "${SHADER_BAKER}" ${baker_arguments}
        RESULT_VARIABLE baker_result
    )
    if (NOT baker_result EQUAL 0)
        message(FATAL_ERROR "stage_builtin_assets: shader_baker failed with exit code ${baker_result}.")
    endif()

    # A baked archive needs neither the GLSL sources nor the include tree;
    # keeping them would ship the very files spv mode exists to avoid.
    file(REMOVE_RECURSE "${staged_builtin_assets}/shaders/include")
    file(GLOB stale_glsl "${staged_builtin_assets}/shaders/*.glsl")
    if (stale_glsl)
        file(REMOVE ${stale_glsl})
    endif()

    message(STATUS "Staged built-in shaders in embedded-SPIR-V form.")
else()
    message(FATAL_ERROR "stage_builtin_assets: unexpected shader mode '${SHADER_MODE}'.")
endif()

# Stamp last: a failed staging run must not look up to date.
file(TOUCH "${staged_builtin_assets}/.staged")
