file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibPhysics
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibPhysics
    PRIVATE
    EngineLibHeaderInterface
    EngineLibExternalDependency
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
        VERBATIM
    )
    list(APPEND PHYSICS_SPIRV_OUTPUTS "${OUT}")
endforeach()

add_custom_target(physics_shader DEPENDS ${PHYSICS_SPIRV_OUTPUTS})
set_target_properties(physics_shader PROPERTIES
    ADDITIONAL_CLEAN_FILES "${PHYSICS_SPIRV_OUTPUTS}"
)
add_dependencies(EngineLibPhysics physics_shader)
