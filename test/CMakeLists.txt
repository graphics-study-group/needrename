set(ASSET_FOLDER "D:/testmesh/")

add_executable(transform_test transform_test.cpp)
target_link_libraries(transform_test engine)
add_test(NAME transform_test COMMAND transform_test)

add_executable(camera_transform_test camera_transform_test.cpp)
target_link_libraries(camera_transform_test engine)
add_test(NAME camera_transform_test COMMAND camera_transform_test)

add_executable(SDL_init_test SDL_init_test.cpp)
target_link_libraries(SDL_init_test engine)
add_test(NAME SDL_init_test COMMAND SDL_init_test)

# Copy SDL dll
add_custom_command(TARGET SDL_init_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:SDL_init_test>
    DEPENDS SDL3::SDL3
    COMMENT "Copying SDL DLLs"
)

if (ASSET_FOLDER STREQUAL "")
    message(WARNING "ASSET_FOLDER not set. Related tests will be skipped.")
else()
    add_executable(mesh_loading_test mesh_loading_test.cpp)
    target_link_libraries(mesh_loading_test engine)
    add_test(NAME mesh_loading_test COMMAND mesh_loading_test ${ASSET_FOLDER})
endif()
