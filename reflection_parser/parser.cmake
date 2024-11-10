function(setup_python_environment)
    set(PARSER_ENV_DIR parser_env)
    if (WIN32)
        set(Python3_ROOT_DIR "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}/Scripts")
    else()
        set(Python3_ROOT_DIR "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}/bin")
    endif()
    find_package(Python3)

    if (NOT Python3_FOUND)
        message(FATAL_ERROR "Python not found!")
    else()
        message(STATUS "Python found: ${Python3_EXECUTABLE}")
    endif()

    if (WIN32)
        set(ACTIVATE_COMMAND "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}/Scripts/activate.bat")
    else()
        set(ACTIVATE_COMMAND "source ${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}/bin/activate")
    endif()
    message(STATUS "Python environment activate command: ${ACTIVATE_COMMAND}")

    set(PYTHON_ENV_SETUP_DONE TRUE PARENT_SCOPE)
    set(Python3_EXECUTABLE ${Python3_EXECUTABLE} PARENT_SCOPE)
    set(ACTIVATE_COMMAND ${ACTIVATE_COMMAND} PARENT_SCOPE)
endfunction()

function(add_reflection_parser target_name reflection_search_files generated_code_dir reflection_search_include_dirs)
    if (NOT PYTHON_ENV_SETUP_DONE)
        setup_python_environment()
    endif()

    string(REPLACE ";" " -I" REFLECTION_SEARCH_INCLUDE_DIRS_ARGS "${reflection_search_include_dirs}")
    set(REFLECTION_SEARCH_INCLUDE_DIRS_ARGS "-I${REFLECTION_SEARCH_INCLUDE_DIRS_ARGS}")
    set(REFLECTION_PARSER_ARGS "-x c++ -w -MG -M -ferror-limit=0 -std=c++20 -o ${CMAKE_BINARY_DIR}/parser_log.txt ${REFLECTION_SEARCH_INCLUDE_DIRS_ARGS}")
    message(STATUS "Reflection parser args: ${REFLECTION_PARSER_ARGS}")

    set(TASK_STAMPED_FILE "${generated_code_dir}/task_stamped")

    if (NOT EXISTS ${generated_code_dir}/generated_reflection.cpp)
        configure_file(${REFLECTION_PARSER_DIR}/template/empty.template ${generated_code_dir}/generated_reflection.cpp)
    endif()

    add_custom_command(
        OUTPUT ${TASK_STAMPED_FILE}

        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
        COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"
        COMMAND ${ACTIVATE_COMMAND} &&
                ${Python3_EXECUTABLE} ${REFLECTION_PARSER_DIR}/parser_main.py
                    --target_name "${target_name}"
                    --reflection_search_files "${reflection_search_files}"
                    --generated_code_dir ${generated_code_dir}
                    --reflection_macros_header ${ENGINE_SOURCE_DIR}/Reflection/macros.h
                    --args ${REFLECTION_PARSER_ARGS}
                    --verbose
        COMMAND ${CMAKE_COMMAND} -E touch ${TASK_STAMPED_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "

        WORKING_DIRECTORY ${REFLECTION_PARSER_DIR}
        DEPENDS ${reflection_search_files}
        COMMENT "Files need reflection have changed, re-run reflection parser"
    )

    add_custom_target(${target_name}_generation ALL
        DEPENDS ${TASK_STAMPED_FILE}
    )
    add_library(${target_name} INTERFACE)
    target_sources(${target_name} INTERFACE ${generated_code_dir}/generated_reflection.cpp)
    target_include_directories(${target_name} INTERFACE ${generated_code_dir})
    target_link_libraries(${target_name} INTERFACE json)
    add_dependencies(${target_name} ${target_name}_generation)
endfunction()

# Get all the include directories from the engine and its dependencies
function(get_include_directories_for_target target include_dirs)
    get_target_property(dirs ${target} INCLUDE_DIRECTORIES)
    get_target_property(interface_dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if (dirs)
        foreach(dir ${dirs})
            if (EXISTS ${dir})
                list(APPEND ${include_dirs} ${dir})
            endif()
        endforeach()
    endif()
    if (interface_dirs)
        foreach(dir ${interface_dirs})
            if (EXISTS ${dir})
                list(APPEND ${include_dirs} ${dir})
            endif()
        endforeach()
    endif()
    get_target_property(deps ${target} INTERFACE_LINK_LIBRARIES)
    if (deps)
        foreach(dep ${deps})
            if (TARGET ${dep})
                get_include_directories_for_target(${dep} ${include_dirs})
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES ${include_dirs})
    set(${include_dirs} ${${include_dirs}} PARENT_SCOPE)
endfunction()
