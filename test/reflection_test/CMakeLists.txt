include(${REFLECTION_PARSER_DIR}/parser.cmake)

add_executable(reflection_test reflection_test.cpp)
target_link_libraries(reflection_test PUBLIC engine)

set(all_include_dirs)
get_include_directories_for_target(reflection_test all_include_dirs)

add_reflection_parser(meta_reflection_test "${CMAKE_CURRENT_SOURCE_DIR}/reflection_test.h" "${CMAKE_CURRENT_SOURCE_DIR}/__generated__" "${all_include_dirs}")
add_dependencies(reflection_test meta_reflection_test)
target_link_libraries(reflection_test PUBLIC meta_reflection_test)

add_test(NAME reflection_test COMMAND reflection_test)
set_target_properties(reflection_test PROPERTIES FOLDER tests)
