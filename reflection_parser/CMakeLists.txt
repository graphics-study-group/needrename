set(PYTHON_EXECUTABLE ${CMAKE_CURRENT_SOURCE_DIR}/parser_env/Scripts/python.exe)
find_package(PythonInterp REQUIRED)

if (NOT PYTHONINTERP_FOUND)
    message(FATAL_ERROR "Python not found!")
else()
    message(STATUS "Python found: ${PYTHON_EXECUTABLE}")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/template/all_engine_headers.txt.in ${CMAKE_CURRENT_BINARY_DIR}/all_engine_headers.txt)
set(ENGINE_ALL_HEADERS_RECORD_FILE ${CMAKE_CURRENT_BINARY_DIR}/all_engine_headers.txt)

add_custom_target(reflection_parser ALL
    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
    COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"

    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/parser_env/Scripts/activate.bat &&
            ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/parser_main.py
                --engine_headers_record_file ${ENGINE_ALL_HEADERS_RECORD_FILE}
                --generated_code_dir ${CMAKE_CURRENT_SOURCE_DIR}/generated

    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "
    
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "test parser python"
)
