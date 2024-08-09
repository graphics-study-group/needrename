function(add_custom_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} engine)
    add_test(NAME ${test_name} COMMAND ${test_name} ${ARGN})
    set_target_properties(${test_name} PROPERTIES FOLDER tests)
endfunction()

add_custom_test(transform_test transform_test.cpp)
add_custom_test(camera_transform_test camera_transform_test.cpp)
add_custom_test(SDL_init_test SDL_init_test.cpp)
add_custom_test(mesh_loading_test mesh_loading_test.cpp)
add_custom_test(external_resource_loading_test external_resource_loading_test.cpp)

# Copy SDL dll
add_custom_command(TARGET SDL_init_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:SDL_init_test>
    DEPENDS SDL3::SDL3
    COMMENT "Copying SDL DLLs"
)
