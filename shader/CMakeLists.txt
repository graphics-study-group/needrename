
if (Vulkan_glslangValidator_FOUND)
    message(STATUS "glslValidator: ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}.")
else()
    message(FATAL_ERROR "Cannot find glslValidator.")
endif()

file(GLOB GLSL_SOURCE_FILES "*.frag" "*.vert")

foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(GLSL_FILE_NAME ${GLSL} NAME)
    set(SPIRV_OUTPUT "${PROJECT_BINARY_DIR}/shader/${GLSL_FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shader/"
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V ${GLSL} -o ${SPIRV_OUTPUT}
        DEPENDS ${GLSL}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV_OUTPUT})
endforeach(GLSL)
message(STATUS "SPIR-V files to be compiled: ${SPIRV_BINARY_FILES}")

add_custom_target(
    shader DEPENDS ${SPIRV_BINARY_FILES}
)
