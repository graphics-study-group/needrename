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

function(generate_cpp_names reflection_search_files)
    set(generated_files "")
    set(config_json "")
    set(index 1)
    foreach(file ${reflection_search_files})
        get_filename_component(filename ${file} NAME)
        set(registrar_impl "${index}_registrar_impl_${filename}.inc")
        set(serialization_impl "${index}_serialization_impl_${filename}.inc")
        get_filename_component(dir_name ${file} DIRECTORY)
        set(wrapper_impl "${dir_name}/__generated__/${filename}.inc")
        list(APPEND generated_files "${registrar_impl}")
        list(APPEND generated_files "${serialization_impl}")
        list(APPEND generated_files "${wrapper_impl}")
        set(config_json "${config_json}
        \{
            \"input_path\": \"${file}\",
            \"output_impl_file\":
            \{
                \"registrar\": \"${registrar_impl}\",
                \"serialization\": \"${serialization_impl}\",
                \"wrapper\": \"${wrapper_impl}\"
            \}
        \},")
        math(EXPR index "${index} + 1")
    endforeach()
    string(LENGTH "${config_json}" str_length)
    math(EXPR new_length "${str_length} - 1") # strip the last comma
    string(SUBSTRING "${config_json}" 0 ${new_length} config_json)

    set(CONFIG_TARGET_FILES ${config_json} PARENT_SCOPE)
endfunction()

function(generate_pkl_file_list parent_projects)
    set(pkl_file_list "")
    foreach(parent_project ${parent_projects})
        get_property(pkl_file TARGET ${parent_project} PROPERTY PKL_CACHE)
        set(pkl_file_list "${pkl_file_list}\"${pkl_file}\",")
    endforeach()
    string(LENGTH "${pkl_file_list}" str_length)
    math(EXPR new_length "${str_length} - 1") # strip the last comma
    string(SUBSTRING "${pkl_file_list}" 0 ${new_length} pkl_file_list)
    set(PARENT_PROJECT_CACHE "${pkl_file_list}" PARENT_SCOPE)
endfunction()

function(add_reflection_parser)
    set(optionArgs)
    set(oneValueArgs target_name generated_code_dir)
    set(multiValueArgs reflection_search_files reflection_search_include_dirs parent_projects)

    cmake_parse_arguments(
        PARSER_ARGS
        "${optionArgs}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
    set(target_name ${PARSER_ARGS_target_name})
    set(reflection_search_files ${PARSER_ARGS_reflection_search_files})
    set(generated_code_dir ${PARSER_ARGS_generated_code_dir})
    set(reflection_search_include_dirs ${PARSER_ARGS_reflection_search_include_dirs})
    if(NOT PARSER_ARGS_parent_projects)
        set(parent_projects "")
    else()
        set(parent_projects ${PARSER_ARGS_parent_projects})
    endif()

    if (NOT PYTHON_ENV_SETUP_DONE)
        setup_python_environment()
    endif()

    if(WIN32)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            # --- Clang on Windows ---
            # Try to use the system's libclang DLL.
            get_filename_component(_CLANG_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            if(EXISTS "${_CLANG_BIN_DIR}/libclang.dll")
                set(ENV{LIBCLANG_LIBRARY_PATH} "${_CLANG_BIN_DIR}")

                # Explicit paths: the C API doesn't auto-detect them.
                set(EXTRA_ARGS "--target=x86_64-w64-windows-gnu")

                execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
                                OUTPUT_VARIABLE _RD OUTPUT_STRIP_TRAILING_WHITESPACE
                                ERROR_QUIET RESULT_VARIABLE _RC)
                if(_RC EQUAL 0 AND EXISTS "${_RD}")
                    set(EXTRA_ARGS "${EXTRA_ARGS} -resource-dir ${_RD}")
                endif()

                get_filename_component(_PREFIX "${_CLANG_BIN_DIR}" DIRECTORY)
                if(EXISTS "${_PREFIX}/include/c++/v1")
                    set(EXTRA_ARGS "${EXTRA_ARGS} -I ${_PREFIX}/include/c++/v1")
                endif()
                if(EXISTS "${_PREFIX}/include")
                    set(EXTRA_ARGS "${EXTRA_ARGS} -I ${_PREFIX}/include")
                endif()
            else()
                # System libclang not found — fall back to old pip-bundled approach.
                set(EXTRA_ARGS "--target=x86_64-w64-windows-gnu -stdlib=libstdc++")
            endif()
        else()
            # --- GCC / MinGW on Windows ---
            # Pip-bundled libclang can find MinGW headers with this target.
            set(EXTRA_ARGS "--target=x86_64-w64-windows-gnu -stdlib=libstdc++")
        endif()
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

    # set up the generated filenames of the file to be parsed
    set(CONFIG_GENERATED_CODE_DIR ${generated_code_dir}/${target_name})
    generate_cpp_names("${reflection_search_files}")

    # set up task stamped file, which is used to check if the parser needs to run
    set(TASK_STAMPED_FILE "${CONFIG_GENERATED_CODE_DIR}/task_stamped")

    if (parent_projects)
        generate_pkl_file_list(${parent_projects})
    endif()

    # generate config.json
    configure_file(${REFLECTION_PARSER_DIR}/template/config.json.template ${CONFIG_GENERATED_CODE_DIR}/config.json)

    file(GLOB_RECURSE template_files ${REFLECTION_PARSER_DIR}/template/*.template)

    if(DEFINED ENV{LIBCLANG_LIBRARY_PATH})
        set(PARSER_ENV_CMD ${CMAKE_COMMAND} -E env "LIBCLANG_LIBRARY_PATH=$ENV{LIBCLANG_LIBRARY_PATH}")
    else()
        set(PARSER_ENV_CMD)
    endif()

    add_custom_command(
        OUTPUT ${TASK_STAMPED_FILE}

        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
        COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"
        COMMAND ${PARSER_ENV_CMD} ${Python3_EXECUTABLE} ${REFLECTION_PARSER_DIR}/parser_main.py
                    --config ${CONFIG_GENERATED_CODE_DIR}/config.json
                    ${REFLECTION_VERBOSE}
        COMMAND ${CMAKE_COMMAND} -E touch ${TASK_STAMPED_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "

        WORKING_DIRECTORY ${REFLECTION_PARSER_DIR}
        DEPENDS ${reflection_search_files} ${template_files}
        COMMENT "Files need reflection have changed, re-run reflection parser"
    )

    add_custom_target(${target_name}_generation ALL
        DEPENDS ${TASK_STAMPED_FILE}
        DEPENDS ${parent_projects} # ensure parent projects are built before this target
    )

    # Clean main meta output and all per-header wrapper output folders.
    set(clean_generated_paths ${CONFIG_GENERATED_CODE_DIR})
    foreach(file ${reflection_search_files})
        get_filename_component(file_dir ${file} DIRECTORY)
        get_filename_component(file_dir ${file_dir} ABSOLUTE)
        list(APPEND clean_generated_paths ${file_dir}/__generated__)
    endforeach()
    list(REMOVE_DUPLICATES clean_generated_paths)

    set_property(
        TARGET ${target_name}_generation
        APPEND
        PROPERTY ADDITIONAL_CLEAN_FILES ${clean_generated_paths}
    )
    set_property(
        TARGET ${target_name}_generation
        PROPERTY PKL_CACHE ${generated_code_dir}/${target_name}/reflection_data.pkl
    )
    set_target_properties(${target_name}_generation PROPERTIES FOLDER parser_generated)

    add_library(${target_name} INTERFACE)
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

function(filter_files_with_reflection_macros input_list output_list)
    set(filtered_list)
    foreach(file_path IN LISTS ${input_list})
        file(READ ${file_path} file_content)
        if("${file_content}" MATCHES "REFL_SER_CLASS")
            list(APPEND filtered_list ${file_path})
        endif()
    endforeach()
    set(${output_list} ${filtered_list} PARENT_SCOPE)
endfunction()
