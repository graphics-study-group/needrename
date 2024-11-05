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

string(REPLACE ";" " -I" REFLECTION_SEARCH_INCLUDE_DIRS_ARGS "${REFLECTION_SEARCH_INCLUDE_DIRS}")
set(REFLECTION_PARSER_ARGS "-x c++ -w -MG -M -ferror-limit=0 -std=c++20 -o ${CMAKE_BINARY_DIR}/parser_log.txt ${REFLECTION_SEARCH_INCLUDE_DIRS_ARGS}")
message(STATUS "Reflection parser args: ${REFLECTION_PARSER_ARGS}")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/task_stamped

    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile started ********** "
    COMMAND ${CMAKE_COMMAND} -E echo "[Precompile]: run parser python script"
    COMMAND ${ACTIVATE_COMMAND} &&
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/parser_main.py
                --reflection_search_files "${REFLECTION_SEARCH_FILES}"
                --generated_code_dir ${CMAKE_CURRENT_SOURCE_DIR}/generated
                --reflection_macros_header ${ENGINE_SOURCE_DIR}/Reflection/macros.h
                --args ${REFLECTION_PARSER_ARGS}
                --verbose
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_SOURCE_DIR}/generated/task_stamped
    COMMAND ${CMAKE_COMMAND} -E echo " ********** Precompile finished ********** "
    
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${REFLECTION_SEARCH_FILES}
    COMMENT "Files need reflection have changed, re-run reflection parser"
)

add_custom_target(reflection_parser ALL
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/task_stamped
)
