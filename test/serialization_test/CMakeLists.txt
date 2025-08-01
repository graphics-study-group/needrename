include(${REFLECTION_PARSER_DIR}/parser.cmake)

add_executable(serialization_test serialization_test.cpp)
target_link_libraries(serialization_test PUBLIC engine)

set(all_include_dirs)
get_include_directories_for_target(serialization_test all_include_dirs)

add_reflection_parser(meta_serialization_test "${CMAKE_CURRENT_SOURCE_DIR}/serialization_test.h" "${CMAKE_CURRENT_SOURCE_DIR}/__generated__" "${all_include_dirs}")
add_dependencies(serialization_test meta_serialization_test)
target_link_libraries(serialization_test PRIVATE meta_serialization_test)

add_test(NAME serialization_test COMMAND serialization_test)
set_target_properties(serialization_test PROPERTIES FOLDER tests)

add_custom_command(TARGET serialization_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:serialization_test>
    COMMENT "Copying SDL DLLs"
)
