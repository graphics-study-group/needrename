include(${REFLECTION_PARSER_DIR}/parser.cmake)

function(add_serialization_split_test case_name source_file header_file)
    set(target_name serialization_${case_name})
    set(meta_target_name meta_serialization_${case_name})
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
    add_test(NAME serialization.${case_name} COMMAND ${target_name})
endfunction()

add_serialization_split_test(stdint serialization_stdint.cpp serialization_stdint.h)
add_serialization_split_test(any serialization_any.cpp serialization_any.h)
add_serialization_split_test(inheritance serialization_inheritance.cpp serialization_inheritance.h)
add_serialization_split_test(shared_ptr serialization_shared_ptr.cpp serialization_shared_ptr.h)
add_serialization_split_test(unique_ptr serialization_unique_ptr.cpp serialization_unique_ptr.h)
add_serialization_split_test(vector serialization_vector.cpp serialization_vector.h)
add_serialization_split_test(custom serialization_custom.cpp serialization_custom.h)
add_serialization_split_test(polymorphism serialization_polymorphism.cpp serialization_polymorphism.h)
add_serialization_split_test(array_ptr serialization_array_ptr.cpp serialization_array_ptr.h)
add_serialization_split_test(self_ptr serialization_self_ptr.cpp serialization_self_ptr.h)
add_serialization_split_test(nested_struct serialization_nested_struct.cpp serialization_nested_struct.h)
add_serialization_split_test(enum serialization_enum.cpp serialization_enum.h)
