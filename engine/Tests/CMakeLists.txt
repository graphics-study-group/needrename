
add_executable(transform_test transform_test.cpp)
target_link_libraries(transform_test engine)
add_test(NAME transform_test COMMAND transform_test)
set_target_properties(transform_test PROPERTIES FOLDER engine_tests)

add_executable(camera_transform_test camera_transform_test.cpp)
target_link_libraries(camera_transform_test engine)
add_test(NAME camera_transform_test COMMAND camera_transform_test)
set_target_properties(camera_transform_test PROPERTIES FOLDER engine_tests)

add_executable(flag_bits_test flag_bits_test.cpp)
target_link_libraries(flag_bits_test engine)
add_test(NAME flag_bits_test COMMAND flag_bits_test)
set_target_properties(flag_bits_test PROPERTIES FOLDER engine_tests)

add_executable(delegate_test delegate_test.cpp)
target_link_libraries(delegate_test engine)
add_test(NAME delegate_test COMMAND delegate_test)
set_target_properties(delegate_test PROPERTIES FOLDER engine_tests)

add_executable(shader_refl_test shader_refl_test.cpp)
target_link_libraries(shader_refl_test engine)
add_test(NAME shader_refl_test COMMAND shader_refl_test)
set_target_properties(shader_refl_test PROPERTIES FOLDER engine_tests)

add_executable(vertex_attribute_test vertex_attribute_test.cpp)
target_link_libraries(vertex_attribute_test engine)
add_test(NAME vertex_attribute_test COMMAND vertex_attribute_test)
set_target_properties(vertex_attribute_test PROPERTIES FOLDER engine_tests)

add_subdirectory(reflection_test)
set_target_properties(reflection_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine/Tests")
add_subdirectory(serialization_test)
set_target_properties(serialization_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine/Tests")
add_subdirectory(shader_compile_test)
set_target_properties(shader_compile_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine/Tests")

add_custom_command(TARGET transform_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3-shared>
    $<TARGET_FILE:engine>
    $<TARGET_FILE:glslang>
    $<TARGET_FILE_DIR:transform_test>
    COMMENT "Copying SDL DLLs"
)
