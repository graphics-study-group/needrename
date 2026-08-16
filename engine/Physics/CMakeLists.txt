file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EnginePhysics SHARED ${SOURCE})
target_compile_definitions(EnginePhysics PRIVATE PHYSICS_DLL_EXPORTS)
target_link_libraries(EnginePhysics
    PUBLIC
    EngineLibHeaderInterface
    AnnoRefl
    EngineDepGlm
    PRIVATE
    EngineRhi
    EngineDepVulkan
    EngineDepSdl
)

# --- Physics shader build pipeline ---
# Scan engine/Physics/shader/ for GLSL sources and compile each to SPIR-V via
# glslangValidator during the build phase.  Output mirrors the source-relative
# path under ${PHYSICS_SPIRV_ROOT} (set in engine/CMakeLists.txt).
if (NOT Vulkan_glslangValidator_FOUND)
    message(FATAL_ERROR "Physics shader pipeline requires glslangValidator but it was not found.")
endif()

file(GLOB_RECURSE PHYSICS_GLSL_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.tesc"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.tese"
    "${CMAKE_CURRENT_SOURCE_DIR}/shader/*.geom"
)

set(PHYSICS_SPIRV_OUTPUTS "")
foreach(SRC ${PHYSICS_GLSL_SOURCES})
    cmake_path(RELATIVE_PATH SRC
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shader"
        OUTPUT_VARIABLE REL
    )
    set(OUT "${PHYSICS_SPIRV_ROOT}/${REL}.spv")
    get_filename_component(OUT_DIR "${OUT}" DIRECTORY)
    add_custom_command(
        OUTPUT "${OUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} $<$<CONFIG:Debug>:-gVS> -V "${SRC}" -o "${OUT}"
        DEPENDS "${SRC}"
    )
    list(APPEND PHYSICS_SPIRV_OUTPUTS "${OUT}")
endforeach()

add_custom_target(physics_shader DEPENDS ${PHYSICS_SPIRV_OUTPUTS})
set_target_properties(physics_shader PROPERTIES
    ADDITIONAL_CLEAN_FILES "${PHYSICS_SPIRV_OUTPUTS}"
)
add_dependencies(EnginePhysics physics_shader)

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
