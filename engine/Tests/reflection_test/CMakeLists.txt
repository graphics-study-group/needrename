include(${REFLECTION_PARSER_DIR}/parser.cmake)

function(add_reflection_split_test case_name source_file header_file)
    set(target_name reflection_${case_name})
    set(meta_target_name meta_reflection_${case_name})
    set(generated_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__/${target_name}")

    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PUBLIC engine)

    set(case_include_dirs)
    get_include_directories_for_target(${target_name} case_include_dirs)

    add_reflection_parser(
        target_name ${meta_target_name}
        reflection_search_files "${CMAKE_CURRENT_SOURCE_DIR}/${header_file}"
        generated_code_dir "${generated_dir}"
        reflection_search_include_dirs "${case_include_dirs}"
    )
    add_dependencies(${target_name} ${meta_target_name})
    target_link_libraries(${target_name} PRIVATE ${meta_target_name})

    set_target_properties(${target_name} PROPERTIES
        FOLDER engine_tests
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engine/Tests"
    )
    add_test(NAME reflection.${case_name} COMMAND ${target_name})
endfunction()

add_reflection_split_test(stdint reflection_stdint.cpp reflection_stdint.h)
add_reflection_split_test(method_invoke reflection_method_invoke.cpp reflection_method_invoke.h)
add_reflection_split_test(const_ref_ptr reflection_const_ref_ptr.cpp reflection_const_ref_ptr.h)
add_reflection_split_test(namespace reflection_namespace.cpp reflection_namespace.h)
add_reflection_split_test(array reflection_array.cpp reflection_array.h)
add_reflection_split_test(smart_pointer reflection_smart_pointer.cpp reflection_smart_pointer.h)
add_reflection_split_test(enum reflection_enum.cpp reflection_enum.h)
add_reflection_split_test(lifecycle reflection_lifecycle.cpp reflection_lifecycle.h)
add_reflection_split_test(nested_member reflection_nested_member.cpp reflection_nested_member.h)
add_reflection_split_test(inheritance reflection_inheritance.cpp reflection_inheritance.h)

