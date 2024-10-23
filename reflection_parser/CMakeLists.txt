set(PYTHON_EXECUTABLE ${CMAKE_CURRENT_SOURCE_DIR}/parser_env/Scripts/python.exe)
find_package(PythonInterp REQUIRED)

if (NOT PYTHONINTERP_FOUND)
    message(FATAL_ERROR "Python not found!")
else()
    message(STATUS "Python found: ${PYTHON_EXECUTABLE}")
endif()

add_custom_target(reflection_parser ALL
    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
    COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"

    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/parser_env/Scripts/activate.bat &&
            ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/parser_main.py
                --reflection_search_files "${REFLECTION_SEARCH_FILES}"
                --generated_code_dir ${CMAKE_CURRENT_SOURCE_DIR}/generated
                --reflection_macros_header ${ENGINE_SOURCE_DIR}/Reflection/macros.h

    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "
    
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "test parser python"
)
