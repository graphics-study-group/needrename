
if (Vulkan_glslangValidator_FOUND)
    message(STATUS "glslValidator: ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}.")
else()
    message(FATAL_ERROR "Cannot find glslValidator.")
endif()

file(GLOB_RECURSE GLSL_SOURCE_FILES
    "*.frag" "*.vert" "*.comp"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
    cmake_path(RELATIVE_PATH GLSL 
        OUTPUT_VARIABLE GLSL_SPV_FILE_NAME
    )
    cmake_path(ABSOLUTE_PATH GLSL
        OUTPUT_VARIABLE GLSL_SOURCE_FULL_PATH
    )
    cmake_path(SET SPIRV_OUTPUT 
        "${BUILTIN_ASSETS_DIR}/shaders/${GLSL_SPV_FILE_NAME}.0.spv"
    )

    message(STATUS ${SPIRV_OUTPUT})

    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} $<$<CONFIG:Debug>:-gVS> -V ${GLSL_SOURCE_FULL_PATH} -o ${SPIRV_OUTPUT}
        DEPENDS ${GLSL}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV_OUTPUT})
endforeach(GLSL)

add_custom_target(
    shader DEPENDS ${SPIRV_BINARY_FILES}
)
set_target_properties(
    shader
    PROPERTIES ADDITIONAL_CLEAN_FILES
    ${SPIRV_BINARY_FILES}
)
