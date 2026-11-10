include_guard(GLOBAL)

# --- Runtime assembly -------------------------------------------------------
#
# The build produces two things per configuration: binaries in `bin/<config>` and
# a staged data root in `stage/<config>`. Assembly copies them into a directory
# laid out exactly like an install prefix, using the same install rules a
# distribution uses. That is the point of the function: a development product and
# a shipped prefix cannot drift, because the development product is
# `cmake --install` of the same rules, just pointed at a different prefix.
#
# Roles, stated once:
#   - `build` compiles and preprocesses (shader bake, physiology modules, staging);
#   - `install` copies and assembles, and nothing else;
#   - this function is the one place that turns "assemble a prefix" into a target.
#
# Assembly is its own step with its own stamp, so a build never pays for it
# unless something it copies changed.

# needrename_stage_runtime(
#     TARGET <target>
#     PREFIX <directory>
#     COMPONENTS <component>...
#     [DEPENDS <file-or-target>...]
#     [EXTRA_COPY <source-dir> <destination-dir>]
# )
#
# TARGET      Target to create; its stamp is created last, so a failed assembly
#             cannot look up to date.
# PREFIX      Directory to assemble into. It is removed first: assembly is a
#             copy, not a merge, so a file the project no longer produces cannot
#             survive in it.
# COMPONENTS  Install components to assemble, in order.
# DEPENDS     Files whose change requires reassembly (staged categories, built
#             binaries). Without them the target is only built on demand.
# EXTRA_COPY  One directory copied verbatim after the components, for content
#             that is not part of an install component (the Python package
#             sources).
function(needrename_stage_runtime)
    cmake_parse_arguments(STAGE "" "TARGET;PREFIX" "COMPONENTS;DEPENDS;EXTRA_COPY" ${ARGN})

    if (NOT STAGE_TARGET)
        message(FATAL_ERROR "needrename_stage_runtime: TARGET is required.")
    endif()
    if (NOT STAGE_PREFIX)
        message(FATAL_ERROR "needrename_stage_runtime: PREFIX is required.")
    endif()
    if (NOT STAGE_COMPONENTS)
        message(FATAL_ERROR "needrename_stage_runtime: COMPONENTS is required.")
    endif()

    set(stage_stamp "${STAGE_PREFIX}/.staged")

    set(stage_commands COMMAND "${CMAKE_COMMAND}" -E rm -rf "${STAGE_PREFIX}")
    foreach(component IN LISTS STAGE_COMPONENTS)
        list(APPEND stage_commands
            COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                --config "$<CONFIG>"
                --prefix "${STAGE_PREFIX}"
                --component "${component}"
        )
    endforeach()

    list(LENGTH STAGE_EXTRA_COPY extra_copy_length)
    if (extra_copy_length EQUAL 2)
        list(GET STAGE_EXTRA_COPY 0 extra_copy_source)
        list(GET STAGE_EXTRA_COPY 1 extra_copy_destination)
        list(APPEND stage_commands
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${extra_copy_source}" "${extra_copy_destination}"
        )
    elseif (NOT extra_copy_length EQUAL 0)
        message(FATAL_ERROR "needrename_stage_runtime: EXTRA_COPY takes two arguments (source, destination).")
    endif()

    list(APPEND stage_commands COMMAND "${CMAKE_COMMAND}" -E touch "${stage_stamp}")

    add_custom_command(
        OUTPUT "${stage_stamp}"
        ${stage_commands}
        DEPENDS ${STAGE_DEPENDS}
        COMMENT "Assembling ${STAGE_TARGET} into ${STAGE_PREFIX}"
        VERBATIM
    )
    add_custom_target(${STAGE_TARGET} ALL DEPENDS "${stage_stamp}")
    set_target_properties(${STAGE_TARGET} PROPERTIES
        FOLDER build
        ADDITIONAL_CLEAN_FILES "${STAGE_PREFIX}"
    )
endfunction()
