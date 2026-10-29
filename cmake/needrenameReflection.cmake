# Reflection code generation for projects that consume the installed SDK.
#
# needrename_add_reflection(<target>
#     HEADERS <header> [<header> ...]
#     [INCLUDE_DIRS <dir> ...]
#     [TYPE_LISTS <type-list.pkl> ...]
#     [GENERATED_DIR <dir>])
#
# Runs the shipped reflection parser over the consumer's headers at build time
# and adds the generated registration and serialization code to <target>.
#
# TYPE_LISTS supplies the known-type lists (the engine modules' reflection
# caches) so consumer types can derive from and reference engine types. It
# defaults to every type list installed beneath the data root. Paths are explicit
# parameters because an installed SDK has no build targets to read them from.
#
# The generated code is written into the consumer's tree: the per-header wrappers
# next to the headers (as the in-tree examples do) and the generated archive code
# under GENERATED_DIR.

include_guard(GLOBAL)

if (NOT EXISTS "${NEEDRENAME_REFLECTION_PARSER_DIR}/parser.cmake")
    message(FATAL_ERROR
        "needrename_add_reflection: the reflection parser is not installed at "
        "'${NEEDRENAME_REFLECTION_PARSER_DIR}'. Install the 'sdk' component of the distribution.")
endif()

include("${NEEDRENAME_REFLECTION_PARSER_DIR}/parser.cmake")

function(needrename_add_reflection target)
    cmake_parse_arguments(ARG "" "GENERATED_DIR" "HEADERS;INCLUDE_DIRS;TYPE_LISTS" ${ARGN})

    if (NOT TARGET ${target})
        message(FATAL_ERROR "needrename_add_reflection: '${target}' is not a target.")
    endif()
    if (NOT ARG_HEADERS)
        message(FATAL_ERROR "needrename_add_reflection(${target}): HEADERS is required.")
    endif()

    if (NOT ARG_TYPE_LISTS)
        file(GLOB ARG_TYPE_LISTS "${NEEDRENAME_REFLECTION_TYPE_LISTS_DIR}/*.pkl")
        if (NOT ARG_TYPE_LISTS)
            message(FATAL_ERROR
                "needrename_add_reflection(${target}): no engine type lists found in "
                "'${NEEDRENAME_REFLECTION_TYPE_LISTS_DIR}'.")
        endif()
    endif()

    if (NOT ARG_GENERATED_DIR)
        set(ARG_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/__generated__/${target}")
    endif()

    if (NOT ARG_INCLUDE_DIRS)
        set(ARG_INCLUDE_DIRS)
        get_include_directories_for_target(${target} ARG_INCLUDE_DIRS)
    endif()

    # The generated code links the exported JSON target, not the in-tree one.
    set(ANROREFL_REFLECTION_LINK_TARGETS needrename::json)

    set(meta_target "meta_${target}")
    add_reflection_parser(
        target_name "${meta_target}"
        reflection_search_files "${ARG_HEADERS}"
        generated_code_dir "${ARG_GENERATED_DIR}"
        reflection_search_include_dirs "${ARG_INCLUDE_DIRS}"
        parent_type_lists "${ARG_TYPE_LISTS}"
    )
    add_dependencies(${target} ${meta_target})
    target_link_libraries(${target} PRIVATE ${meta_target})
endfunction()
