include(${CMAKE_CURRENT_SOURCE_DIR}/../parser/parser.cmake)

function(add_annorefl_reflection_test case_name source_file header_file)
    set(target_name annorefl_reflection_${case_name})
    set(meta_target_name meta_annorefl_reflection_${case_name})
    set(generated_dir "${CMAKE_CURRENT_SOURCE_DIR}/reflection/__generated__")

    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PUBLIC AnnoRefl)

    set(case_include_dirs)
    get_include_directories_for_target(${target_name} case_include_dirs)

    add_reflection_parser(
        target_name ${meta_target_name}
        reflection_search_files "${CMAKE_CURRENT_SOURCE_DIR}/reflection/${header_file}"
        generated_code_dir "${generated_dir}"
        reflection_search_include_dirs "${case_include_dirs}"
    )
    add_dependencies(${target_name} ${meta_target_name})
    target_link_libraries(${target_name} PRIVATE ${meta_target_name})

    set_target_properties(${target_name} PROPERTIES FOLDER annorefl_tests)
    add_test(NAME annorefl.reflection.${case_name} COMMAND ${target_name})
endfunction()

function(add_annorefl_serialization_test case_name source_file header_file)
    set(target_name annorefl_serialization_${case_name})
    set(meta_target_name meta_annorefl_serialization_${case_name})
    set(generated_dir "${CMAKE_CURRENT_SOURCE_DIR}/serialization/__generated__")

    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PUBLIC AnnoRefl)

    set(case_include_dirs)
    get_include_directories_for_target(${target_name} case_include_dirs)

    add_reflection_parser(
        target_name ${meta_target_name}
        reflection_search_files "${CMAKE_CURRENT_SOURCE_DIR}/serialization/${header_file}"
        generated_code_dir "${generated_dir}"
        reflection_search_include_dirs "${case_include_dirs}"
    )
    add_dependencies(${target_name} ${meta_target_name})
    target_link_libraries(${target_name} PRIVATE ${meta_target_name})

    set_target_properties(${target_name} PROPERTIES FOLDER annorefl_tests)
    add_test(NAME annorefl.serialization.${case_name} COMMAND ${target_name})
endfunction()

add_annorefl_reflection_test(stdint reflection/reflection_stdint.cpp reflection_stdint.h)
add_annorefl_reflection_test(method_invoke reflection/reflection_method_invoke.cpp reflection_method_invoke.h)
add_annorefl_reflection_test(const_ref_ptr reflection/reflection_const_ref_ptr.cpp reflection_const_ref_ptr.h)
add_annorefl_reflection_test(namespace reflection/reflection_namespace.cpp reflection_namespace.h)
add_annorefl_reflection_test(array reflection/reflection_array.cpp reflection_array.h)
add_annorefl_reflection_test(smart_pointer reflection/reflection_smart_pointer.cpp reflection_smart_pointer.h)
add_annorefl_reflection_test(enum reflection/reflection_enum.cpp reflection_enum.h)
add_annorefl_reflection_test(lifecycle reflection/reflection_lifecycle.cpp reflection_lifecycle.h)
add_annorefl_reflection_test(nested_member reflection/reflection_nested_member.cpp reflection_nested_member.h)
add_annorefl_reflection_test(inheritance reflection/reflection_inheritance.cpp reflection_inheritance.h)

add_annorefl_serialization_test(stdint serialization/serialization_stdint.cpp serialization_stdint.h)
add_annorefl_serialization_test(any serialization/serialization_any.cpp serialization_any.h)
add_annorefl_serialization_test(inheritance serialization/serialization_inheritance.cpp serialization_inheritance.h)
add_annorefl_serialization_test(shared_ptr serialization/serialization_shared_ptr.cpp serialization_shared_ptr.h)
add_annorefl_serialization_test(unique_ptr serialization/serialization_unique_ptr.cpp serialization_unique_ptr.h)
add_annorefl_serialization_test(vector serialization/serialization_vector.cpp serialization_vector.h)
add_annorefl_serialization_test(custom serialization/serialization_custom.cpp serialization_custom.h)
add_annorefl_serialization_test(polymorphism serialization/serialization_polymorphism.cpp serialization_polymorphism.h)
add_annorefl_serialization_test(array_ptr serialization/serialization_array_ptr.cpp serialization_array_ptr.h)
add_annorefl_serialization_test(self_ptr serialization/serialization_self_ptr.cpp serialization_self_ptr.h)
add_annorefl_serialization_test(nested_struct serialization/serialization_nested_struct.cpp serialization_nested_struct.h)
add_annorefl_serialization_test(enum serialization/serialization_enum.cpp serialization_enum.h)
