set(_ANROREFL_PARSER_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Verify the user-provided Python interpreter can import the parser requirements.
# The interpreter is discovered through the standard CMake Python3 search (e.g.
# Python3_EXECUTABLE set in CMakeUserPresets.json). No venv is created here.
function(ensure_parser_python)
    if (PARSER_PYTHON_READY)
        return()
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import clang, mako"
        RESULT_VARIABLE _import_rc
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if (NOT _import_rc EQUAL 0)
        message(FATAL_ERROR
            "Python interpreter '${Python3_EXECUTABLE}' cannot import the reflection parser requirements.\n"
            "Point CMake at an interpreter that has 'clang' and 'mako' installed\n"
            "(e.g. set Python3_EXECUTABLE in CMakeUserPresets.json to your .venv's python).\n"
            "Install the requirements with:\n"
            "  ${Python3_EXECUTABLE} -m pip install -r ${_ANROREFL_PARSER_CMAKE_DIR}/requirements.txt")
    endif()

    set(PARSER_PYTHON_READY TRUE PARENT_SCOPE)
    set(Python3_EXECUTABLE ${Python3_EXECUTABLE} PARENT_SCOPE)
endfunction()

# Compute the libclang parsing arguments (EXTRA_ARGS) and the environment wrapper
# needed to run the reflection parser with the correct libclang. On Windows (MSVC)
# the parser uses the libclang.dll shipped by Visual Studio's "C++ Clang tools for
# Windows" component; on other platforms it relies on pip's bundled libclang but
# pins the builtin-header resource dir to the compiler.
function(_anrorefl_find_vs_libclang out_dir)
    # Locate <vs-root>/VC/Tools/Llvm/x64/bin where the VS-bundled libclang.dll lives.
    # Preference order: CMAKE_GENERATOR_INSTANCE (VS generator), then derive the VC
    # root by ascending from cl.exe's directory (VC/Tools/MSVC/<ver>/bin/Hostx64/x64).
    set(_vs_vc_dirs)
    if(DEFINED CMAKE_GENERATOR_INSTANCE)
        list(APPEND _vs_vc_dirs "${CMAKE_GENERATOR_INSTANCE}/VC")
    endif()
    if(CMAKE_CXX_COMPILER AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY) # .../bin/Hostx64/x64
        get_filename_component(_asc "${_cl_dir}" DIRECTORY)               # Hostx64
        get_filename_component(_asc "${_asc}" DIRECTORY)                   # bin
        get_filename_component(_asc "${_asc}" DIRECTORY)                   # <ver>
        get_filename_component(_asc "${_asc}" DIRECTORY)                   # MSVC
        get_filename_component(_asc "${_asc}" DIRECTORY)                   # Tools
        get_filename_component(_asc "${_asc}" DIRECTORY)                   # VC
        list(APPEND _vs_vc_dirs "${_asc}")
    endif()
    foreach(_vc IN LISTS _vs_vc_dirs)
        if(EXISTS "${_vc}/Tools/Llvm/x64/bin/libclang.dll")
            set(${out_dir} "${_vc}/Tools/Llvm/x64/bin" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

function(anrorefl_libclang_extra_args)
    if(WIN32)
        # --- Windows (MSVC) ---
        # pip's bundled libclang (clang 18.x) is too old to parse current MSVC STL
        # headers, so load the libclang.dll from the VS "C++ Clang tools for Windows"
        # component. The Python bindings (pip `clang`) stay in charge; clang.cindex
        # redirects the DLL it loads via the LIBCLANG_LIBRARY_PATH environment var.
        _anrorefl_find_vs_libclang(_VS_LLVM_BIN_DIR)
        if(NOT _VS_LLVM_BIN_DIR)
            message(FATAL_ERROR
                "The reflection parser needs Visual Studio's bundled libclang.dll.\n"
                "Install the 'C++ Clang tools for Windows' component via the Visual Studio Installer\n"
                "(individual component; provides VC/Tools/Llvm/x64/bin/libclang.dll), then reconfigure.")
        endif()
        set(ENV{LIBCLANG_LIBRARY_PATH} "${_VS_LLVM_BIN_DIR}")

        # Parse in MSVC-compatible mode. clang auto-detects the active Visual Studio
        # installation and Windows SDK when targeting x86_64-pc-windows-msvc, so no
        # explicit -I list is needed and the args stay free of spaces/quotes (they are
        # embedded as a single JSON string in config.json, so quotes would corrupt it).
        set(EXTRA_ARGS
            "--target=x86_64-pc-windows-msvc -fms-compatibility -fms-extensions -fmsc-version=${MSVC_VERSION}")
    else()
        # --- Other platforms ---
        # Pin builtin headers to the project toolchain so pip's libclang sees the
        # same C++ standard library headers as the actual compiler. GCC toolchain
        # auto-detection supplies the libstdc++ include paths.
        set(EXTRA_ARGS "")
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
                        OUTPUT_VARIABLE _RD OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE _RC)
        if(_RC EQUAL 0 AND EXISTS "${_RD}")
            set(EXTRA_ARGS "-resource-dir ${_RD}")
        endif()

        # Define FLT_MAX and FLT_MIN to work around float.h inclusion
        set(EXTRA_ARGS "${EXTRA_ARGS} -DFLT_MAX -DFLT_MIN")
    endif()
    set(ANROREFL_EXTRA_ARGS ${EXTRA_ARGS} PARENT_SCOPE)

    if(DEFINED ENV{LIBCLANG_LIBRARY_PATH})
        set(ANROREFL_PARSER_ENV_CMD ${CMAKE_COMMAND} -E env "LIBCLANG_LIBRARY_PATH=$ENV{LIBCLANG_LIBRARY_PATH}" PARENT_SCOPE)
    else()
        set(ANROREFL_PARSER_ENV_CMD PARENT_SCOPE)
    endif()
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

    # Resolve the libclang parse arguments first (sets LIBCLANG_LIBRARY_PATH on
    # Windows), then verify the Python interpreter can import the requirements.
    anrorefl_libclang_extra_args()
    ensure_parser_python()

    if (REFLECTION_VERBOSE)
        set(REFLECTION_VERBOSE --verbose)
    else()
        set(REFLECTION_VERBOSE)
    endif()

    # translate include directories into -I format
    string(REPLACE ";" " -I" REFLECTION_SEARCH_INCLUDE_DIRS_ARGS "${reflection_search_include_dirs}")
    set(REFLECTION_SEARCH_INCLUDE_DIRS_ARGS "-I${REFLECTION_SEARCH_INCLUDE_DIRS_ARGS}")
    # set up parser args
    set(REFLECTION_PARSER_ARGS "-xc++ -MG -M -ferror-limit=0 -std=c++20 ${ANROREFL_EXTRA_ARGS} -o ${CMAKE_BINARY_DIR}/parser_log.txt ${REFLECTION_SEARCH_INCLUDE_DIRS_ARGS}")
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
    configure_file(${_ANROREFL_PARSER_CMAKE_DIR}/template/config.json.template ${CONFIG_GENERATED_CODE_DIR}/config.json)

    file(GLOB_RECURSE template_files ${_ANROREFL_PARSER_CMAKE_DIR}/template/*.template)

    add_custom_command(
        OUTPUT ${TASK_STAMPED_FILE}

        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
        COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"
        COMMAND ${ANROREFL_PARSER_ENV_CMD} ${Python3_EXECUTABLE} ${_ANROREFL_PARSER_CMAKE_DIR}/parser_main.py
                    --config ${CONFIG_GENERATED_CODE_DIR}/config.json
                    ${REFLECTION_VERBOSE}
        COMMAND ${CMAKE_COMMAND} -E touch ${TASK_STAMPED_FILE}
        COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "

        WORKING_DIRECTORY ${_ANROREFL_PARSER_CMAKE_DIR}
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
