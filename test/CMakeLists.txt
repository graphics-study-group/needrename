set(ASSET_FOLDER "")

add_executable(transform_test transform_test.cpp)
target_link_libraries(transform_test engine)
add_test(NAME transform_test COMMAND transform_test)

if (ASSET_FOLDER STREQUAL "")
    message(WARNING "ASSET_FOLDER not set. Related tests will be skipped.")
else()
    add_executable(mesh_loading_test mesh_loading_test.cpp)
    target_link_libraries(mesh_loading_test engine)
    add_test(NAME mesh_loading_test COMMAND mesh_loading_test ${ASSET_FOLDER})
endif()
