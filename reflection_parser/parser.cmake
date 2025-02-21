function(create_python_venv)
    # Find system Python3
    find_package(Python3 COMPONENTS Interpreter)
    execute_process(COMMAND ${Python3_EXECUTABLE} -m venv "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}")
endfunction()

function(setup_python_environment)
    set(PARSER_ENV_DIR parser_env)

    # Set up venv for the first time
    if (NOT EXISTS "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}")
        message(STATUS "Setting up virtual environment for the first time...")
        create_python_venv()
        if (NOT EXISTS "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}")
            message(FATAL_ERROR "Failed to create virtual environment. Please check whether venv is supported and installed.")
        endif()
    endif()

    # Find Python3 in virtual environment
    set(ENV{VIRTUAL_ENV} "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}")
    set(Python3_FIND_VIRTUALENV ONLY)
    unset(Python3_FOUND)
    unset(Python3_EXECUTABLE)
    find_package(Python3 COMPONENTS Interpreter)

    if (NOT Python3_FOUND)
        message(FATAL_ERROR "Python not found! Check if venv is setup correctly.")
    else()
        message(DEBUG "Python found: ${Python3_EXECUTABLE}")
    endif()
    
    if (NOT EXISTS "${REFLECTION_PARSER_DIR}/${PARSER_ENV_DIR}/Lib/site-packages/clang")
        message(STATUS "Installing requirements in venv.")
        execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install -r "${REFLECTION_PARSER_DIR}/requirements.txt")
    endif()

    set(PYTHON_ENV_SETUP_DONE TRUE PARENT_SCOPE)
    set(Python3_EXECUTABLE ${Python3_EXECUTABLE} PARENT_SCOPE)
endfunction()

function(generate_cpp_names reflection_search_files generated_code_dir)
    set(generated_files "")
    set(config_json "")
    set(index 1)
    foreach(file ${reflection_search_files})
        get_filename_component(filename ${file} NAME)
        set(registrar_impl "${generated_code_dir}/${index}_registrar_impl_${filename}.cpp")
        set(serialization_impl "${generated_code_dir}/${index}_serialization_impl_${filename}.cpp")
        list(APPEND generated_files "${registrar_impl}")
        list(APPEND generated_files "${serialization_impl}")
        set(config_json "${config_json}
        \{
            \"input_path\": \"${file}\",
            \"output_impl_path\":
            \{
                \"registrar\": \"${registrar_impl}\",
                \"serialization\": \"${serialization_impl}\"
            \}
        \},")
        math(EXPR index "${index} + 1")
    endforeach()
    string(LENGTH "${config_json}" str_length)
    math(EXPR new_length "${str_length} - 1") # strip the last comma
    string(SUBSTRING "${config_json}" 0 ${new_length} config_json)

    set(GENERATED_CPPS ${generated_files} PARENT_SCOPE)
    set(CONFIG_TARGET_FILES ${config_json} PARENT_SCOPE)
endfunction()

function(add_reflection_parser target_name reflection_search_files generated_code_dir reflection_search_include_dirs)
    if (NOT PYTHON_ENV_SETUP_DONE)
        setup_python_environment()
    endif()

    if(WIN32)
        # On Windows we have to force clang to use MinGW, since it defaults to MSVC.
        set(EXTRA_ARGS "--target=x86_64-w64-windows-gnu -stdlib=libstdc++")
    else()
        # On other platforms we leave it as default.
        set(EXTRA_ARGS "")
    endif()
    # Define FLT_MAX and FLT_MIN to work around float.h inclusion
    set(EXTRA_ARGS "${EXTRA_ARGS} -DFLT_MAX -DFLT_MIN")

    if (REFLECTION_VERBOSE)
        set(REFLECTION_VERBOSE --verbose)
    else()
        set(REFLECTION_VERBOSE)
    endif()

    # translate include directories into -I format
    string(REPLACE ";" " -I" REFLECTION_SEARCH_INCLUDE_DIRS_ARGS "${reflection_search_include_dirs}")
    set(REFLECTION_SEARCH_INCLUDE_DIRS_ARGS "-I${REFLECTION_SEARCH_INCLUDE_DIRS_ARGS}")
    # set up parser args
    set(REFLECTION_PARSER_ARGS "-xc++ -MG -M -ferror-limit=0 -std=c++20 ${EXTRA_ARGS} -o ${CMAKE_BINARY_DIR}/parser_log.txt ${REFLECTION_SEARCH_INCLUDE_DIRS_ARGS}")
    message(DEBUG "Reflection parser args: ${REFLECTION_PARSER_ARGS}")

    # set up task stamped file, which is used to check if the parser needs to run
    set(TASK_STAMPED_FILE "${generated_code_dir}/task_stamped")

    # set up the generated filenames of the file to be parsed
    set(CONFIG_GENERATED_CODE_DIR ${generated_code_dir}/${target_name})
    generate_cpp_names("${reflection_search_files}" "${CONFIG_GENERATED_CODE_DIR}")
    foreach(cpp_file ${GENERATED_CPPS})
        if (NOT EXISTS ${cpp_file})
            # generate empty file, used in cmake configuration
            configure_file(${REFLECTION_PARSER_DIR}/template/empty.template ${cpp_file})
        endif()
    endforeach()

    # generate config.json
    configure_file(${REFLECTION_PARSER_DIR}/template/config.json.template ${generated_code_dir}/config.json)

    file(GLOB_RECURSE template_files ${REFLECTION_PARSER_DIR}/template/*.template)

    add_custom_command(
        OUTPUT ${TASK_STAMPED_FILE}

        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
        COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"
        COMMAND ${Python3_EXECUTABLE} ${REFLECTION_PARSER_DIR}/parser_main.py
                    --config ${generated_code_dir}/config.json
                    ${REFLECTION_VERBOSE}
        COMMAND ${CMAKE_COMMAND} -E touch ${TASK_STAMPED_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "

        WORKING_DIRECTORY ${REFLECTION_PARSER_DIR}
        DEPENDS ${reflection_search_files} ${template_files}
        COMMENT "Files need reflection have changed, re-run reflection parser"
    )

    add_custom_target(${target_name}_generation ALL
        DEPENDS ${TASK_STAMPED_FILE}
    )
    set_property(
        TARGET ${target_name}_generation
        APPEND
        PROPERTY ADDITIONAL_CLEAN_FILES ${generated_code_dir}
    )

    add_library(${target_name} INTERFACE)
    target_sources(${target_name} INTERFACE ${GENERATED_CPPS})
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
