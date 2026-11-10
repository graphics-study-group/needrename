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

# Single source of truth for the per-config subdirectory of the bake output.
# Staging flattens it away into the data root's `spirv/`, so the runtime never
# sees it.
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

# Sources that live in the tree but are not part of this build yet. The
# single-workgroup joint sweep is deferred: it binds 18 storage buffers (over the
# browser's 16-buffer limit) and uses a workgroup memory barrier the WGSL baker
# cannot translate, so neither its SPIR-V nor its WGSL variant is produced and the
# solver's per-color dispatch path is the only joint solver. Remove the file from
# this list together with the binding compression that brings it back.
set(PHYSICS_GLSL_DEFERRED
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/solver/XPBDSolver/joint_color_sweep.comp"
)
list(REMOVE_ITEM PHYSICS_GLSL_SOURCES ${PHYSICS_GLSL_DEFERRED})

# A stale artefact from a build that still produced a deferred shader must not
# survive: the solver no longer looks one up, but the staged data root is a
# product and an orphan there is a lie about what the build uses.
set(PHYSICS_SPIRV_DEFERRED_ARTEFACTS "")
foreach(DEFERRED_SRC ${PHYSICS_GLSL_DEFERRED})
    cmake_path(RELATIVE_PATH DEFERRED_SRC
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shader"
        OUTPUT_VARIABLE DEFERRED_REL
    )
    list(APPEND PHYSICS_SPIRV_DEFERRED_ARTEFACTS
        "${PHYSICS_SPIRV_ROOT}/debug/${DEFERRED_REL}.spv"
        "${PHYSICS_SPIRV_ROOT}/release/${DEFERRED_REL}.spv"
        "${PHYSICS_SPIRV_ROOT}/debug/${DEFERRED_REL}.wgsl"
        "${PHYSICS_SPIRV_ROOT}/release/${DEFERRED_REL}.wgsl"
        "${PHYSICS_SPIRV_ROOT}/debug/${DEFERRED_REL}.iface.json"
        "${PHYSICS_SPIRV_ROOT}/release/${DEFERRED_REL}.iface.json"
    )
endforeach()
file(REMOVE ${PHYSICS_SPIRV_DEFERRED_ARTEFACTS})

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

# Stage the configuration's modules into the data root for every backend, not
# only the one that loads WGSL: the runtime resolves physics modules through the
# data root unconditionally, so a Vulkan product must carry its `/spirv` tree the
# same way a WebGPU one does. Without this a Vulkan run falls back to a
# compile-time path into the build tree, which is exactly the kind of path the
# data root exists to replace.
set(PHYSICS_STAGE_DIR "${ENGINE_DATA_STAGE_DIR}/spirv")
set(PHYSICS_STAGED_STAMP "${ENGINE_DATA_STAGE_DIR}/.stamps/physics_shader")
add_custom_command(
    OUTPUT "${PHYSICS_STAGED_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE=${PHYSICS_SPIRV_ROOT}/${PHYSICS_SPIRV_CONFIG_SUBDIR}"
        "-DDESTINATION=${PHYSICS_STAGE_DIR}"
        "-DEXCLUDES=*.d;*.log"
        "-DSTAMP=${PHYSICS_STAGED_STAMP}"
        -P "${CMAKE_SOURCE_DIR}/cmake/stage_tree.cmake"
    DEPENDS ${PHYSICS_SPIRV_OUTPUTS}
    COMMENT "Staging the baked physics shaders into the data root"
    VERBATIM
)
add_custom_target(physics_shader_stage ALL DEPENDS "${PHYSICS_STAGED_STAMP}")
set_target_properties(physics_shader_stage PROPERTIES
    FOLDER build
    ADDITIONAL_CLEAN_FILES "${PHYSICS_STAGE_DIR}"
)
add_dependencies(EnginePhysics physics_shader_stage)

# Install the staged tree: an installation carries one configuration, so the
# per-configuration build-tree split is already flattened away by staging.
install(
    DIRECTORY "${ENGINE_DATA_STAGE_DIR}/spirv/"
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
