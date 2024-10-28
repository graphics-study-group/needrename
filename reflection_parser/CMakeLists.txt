set(PARSER_ENV_DIR parser_env)
if (WIN32)
    set(Python3_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${PARSER_ENV_DIR}/Scripts")
else()
    set(Python3_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${PARSER_ENV_DIR}/bin")
endif()
find_package(Python3)

if (NOT Python3_FOUND)
    message(FATAL_ERROR "Python not found!")
else()
    message(STATUS "Python found: ${Python3_EXECUTABLE}")
endif()

if (WIN32)
    set(ACTIVATE_COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/${PARSER_ENV_DIR}/Scripts/activate.bat")
else()
    set(ACTIVATE_COMMAND "source ${CMAKE_CURRENT_SOURCE_DIR}/${PARSER_ENV_DIR}/bin/activate")
endif()
message(STATUS "Python environment activate command: ${ACTIVATE_COMMAND}")

add_custom_target(reflection_parser ALL
    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
    COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"

    COMMAND ${ACTIVATE_COMMAND} &&
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/parser_main.py
                --reflection_search_files "${REFLECTION_SEARCH_FILES}"
                --generated_code_dir ${CMAKE_CURRENT_SOURCE_DIR}/generated
                --reflection_macros_header ${ENGINE_SOURCE_DIR}/Reflection/macros.h

    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "
    
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "test parser python"
)
