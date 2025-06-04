
if (Vulkan_glslangValidator_FOUND)
    message(STATUS "glslValidator: ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}.")
else()
    message(FATAL_ERROR "Cannot find glslValidator.")
endif()

file(GLOB GLSL_SOURCE_FILES "*.frag" "*.vert" "*.comp")

foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(GLSL_FILE_NAME ${GLSL} NAME)
    set(SPIRV_OUTPUT "${PROJECT_BINARY_DIR}/shader/${GLSL_FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shader/"
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V ${GLSL} -o ${SPIRV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPIRV_OUTPUT} "${BUILTIN_ASSETS_DIR}/shaders/${GLSL_FILE_NAME}.spv"
        DEPENDS ${GLSL}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV_OUTPUT})
endforeach(GLSL)
message(STATUS "SPIR-V files to be compiled: ${SPIRV_BINARY_FILES}")

add_custom_target(
    shader DEPENDS ${SPIRV_BINARY_FILES}
)

add_custom_command(TARGET shader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different 
    "${PROJECT_BINARY_DIR}/shader"
    "${PROJECT_BINARY_DIR}/test/shader"
    COMMENT "Copying shaders"
)
