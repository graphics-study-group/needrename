file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EnginePhysics ${SOURCE})
target_compile_definitions(EnginePhysics PRIVATE PHYSICS_DLL_EXPORTS)
target_link_libraries(EnginePhysics
    PUBLIC
    EngineLibHeaderInterface
    AnnoRefl
    EngineDepGlm
    PRIVATE
    EngineRhi
    EngineCore
    EngineConfigHeader
    EngineDepRhiBackend
    EngineDepSdl
)

# --- Physics shader build pipeline ---
# Scan engine/Physics/shader/ for GLSL sources and compile each to SPIR-V with
# the native C++ shader_baker (the same tool that bakes the built-in shader
# archives), declared as one incremental unit per source. No separately
# installed glslangValidator is involved, and a WebGPU build declares each
# source's WGSL variant and interface description as outputs of the same
# command instead of a directory-wide stamp.
#
# Each configuration gets its own subdirectory because the Debug arguments
# differ (Debug bakes debug info); sharing one directory would let a Debug build
# overwrite the Release artefacts, or vice versa.
if (NOT ENGINE_SHADER_BAKER_EXECUTABLE)
    message(FATAL_ERROR
        "The physics shader pipeline needs a shader_baker: a native build produces its own; "
        "a browser build needs the host-tools preset (see the top-level configure diagnostic).")
endif()

# Single source of truth for the per-config subdirectory: used both for the
# output path below and for the ENGINE_PHYSICS_SPIRV_DIR the runtime resolves
# against (see engine/cmake_config.h.in), so build and loader cannot drift.
# RelWithDebInfo/MinSizeRel compile without debug info, hence share the release tree.
set(PHYSICS_SPIRV_CONFIG_SUBDIR "$<IF:$<CONFIG:Debug>,debug,release>")

file(GLOB_RECURSE PHYSICS_GLSL_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.tesc"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.tese"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.geom"
)

# The baker records the include files each source actually reads in a depfile.
# An include edit therefore rebuilds its consumers without invalidating every
# unrelated physics shader.
set(PHYSICS_SPIRV_OUTPUTS "")
foreach(SRC ${PHYSICS_GLSL_SOURCES})
    cmake_path(RELATIVE_PATH SRC
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shader"
        OUTPUT_VARIABLE REL
    )
    set(OUT_BASE "${PHYSICS_SPIRV_ROOT}/${PHYSICS_SPIRV_CONFIG_SUBDIR}/${REL}")
    set(OUT "${OUT_BASE}.spv")
    get_filename_component(OUT_DIR "${OUT}" DIRECTORY)
    set(PHYSICS_BAKE_ARGUMENTS
        --source "${SRC}"
        --output-base "${OUT_BASE}"
        --depfile "${OUT_BASE}.d"
        $<$<CONFIG:Debug>:--debug-info>
    )
    if (ENGINE_RHI_BACKEND_WEBGPU)
        # Two arguments, not one string: `add_custom_command` re-parses a single
        # argument's contents, and a generator expression that joins them with a
        # semicolon arrives at the tool as `--converter;C:/...`.
        list(APPEND PHYSICS_BAKE_ARGUMENTS --wgsl --converter "${SHADER_WGSL_CONVERTER}")
        add_custom_command(
            OUTPUT "${OUT}" "${OUT_BASE}.wgsl" "${OUT_BASE}.iface.json"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
            COMMAND "${ENGINE_SHADER_BAKER_EXECUTABLE}" ${PHYSICS_BAKE_ARGUMENTS}
            DEPENDS "${SRC}" ${ENGINE_SHADER_BAKER_DEPENDENCY} "${SHADER_WGSL_CONVERTER}"
            DEPFILE "${OUT_BASE}.d"
            COMMENT "Baking physics shader ${REL}"
            VERBATIM
        )
        list(APPEND PHYSICS_SPIRV_OUTPUTS "${OUT}" "${OUT_BASE}.wgsl" "${OUT_BASE}.iface.json")
    else()
        add_custom_command(
            OUTPUT "${OUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
            COMMAND "${ENGINE_SHADER_BAKER_EXECUTABLE}" ${PHYSICS_BAKE_ARGUMENTS}
            DEPENDS "${SRC}" ${ENGINE_SHADER_BAKER_DEPENDENCY}
            DEPFILE "${OUT_BASE}.d"
            COMMENT "Baking physics shader ${REL}"
            VERBATIM
        )
        list(APPEND PHYSICS_SPIRV_OUTPUTS "${OUT}")
    endif()
endforeach()

add_custom_target(physics_shader DEPENDS ${PHYSICS_SPIRV_OUTPUTS})
set_target_properties(physics_shader PROPERTIES
    ADDITIONAL_CLEAN_FILES "${PHYSICS_SPIRV_OUTPUTS}"
)
add_dependencies(EnginePhysics physics_shader)

# WebGPU loads the baked WGSL tree through the configuration's data root.
if (ENGINE_RHI_BACKEND_WEBGPU)
    set(PHYSICS_STAGE_DIR "${ENGINE_DATA_STAGE_DIR}/spirv")
    add_custom_target(physics_shader_stage ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PHYSICS_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PHYSICS_SPIRV_ROOT}/${PHYSICS_SPIRV_CONFIG_SUBDIR}"
                "${PHYSICS_STAGE_DIR}"
        DEPENDS physics_shader
        COMMENT "Staging the baked physics shaders into the data root"
        VERBATIM
    )
    set_target_properties(physics_shader_stage PROPERTIES FOLDER build)
    add_dependencies(EnginePhysics physics_shader_stage)
endif()

# The C++ side loads SPIR-V through the runtime data locator's physics root;
# ENGINE_PHYSICS_SPIRV_DIR is its developer fallback and must select the same
# per-configuration subdirectory the artefacts above are written to.
target_compile_definitions(EnginePhysics PRIVATE
    ENGINE_PHYSICS_SPIRV_CONFIG_SUBDIR="${PHYSICS_SPIRV_CONFIG_SUBDIR}"
)

# Install the configuration's artefacts into the runtime data root at
# source-relative paths. An installation carries one configuration, so the
# per-configuration build-tree split is flattened away.
install(
    DIRECTORY "${PHYSICS_SPIRV_ROOT}/${PHYSICS_SPIRV_CONFIG_SUBDIR}/"
    DESTINATION share/needrename/spirv
    COMPONENT runtime
)

# --- Reflection: per-DLL registration for Physics types ---
file(GLOB_RECURSE PHYSICS_HEADERS "./*.h")
include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(PHYSICS_HEADERS PHYSICS_REFLECTION_HEADERS)

set(physics_include_dirs)
get_include_directories_for_target(EnginePhysics physics_include_dirs)

add_reflection_parser(
    target_name meta_physics
    reflection_search_files "${PHYSICS_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${physics_include_dirs}"
)
add_dependencies(EnginePhysics meta_physics)
target_link_libraries(EnginePhysics PRIVATE meta_physics)
