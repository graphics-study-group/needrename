# Register a single test executable with CTest.
#
# Called once per test from the category subdirectories. Each invocation issues
# its own add_executable()/add_test() so every test stays individually visible
# to VS Code's CTest integration (do not collapse into a foreach loop).
#
# Options:
#   WINDOWED            marks the test with the "windowed" label (needs a display)
#   FRAMES <n>          passes <n> as the frame-count command-line argument
#   TIMEOUT <s>         CTest timeout in seconds
#   WORKING_DIRECTORY   CTest working directory
#   FOLDER <group>      IDE folder for the target
#   SOURCES ...         source files (default: ${target}.cpp)
#   LINK ...            link libraries (default: Engine)
#   ARGS ...            extra command-line arguments
#   EXTRA_LABELS ...    extra CTest labels (e.g. "gpu" for GPU-dependent tests)
function(anro_add_test target)
    cmake_parse_arguments(TEST "WINDOWED" "FRAMES;TIMEOUT;WORKING_DIRECTORY;FOLDER"
                          "SOURCES;LINK;ARGS;EXTRA_LABELS" ${ARGN})

    if(NOT TEST_SOURCES)
        set(TEST_SOURCES ${target}.cpp)
    endif()
    if(NOT TEST_LINK)
        set(TEST_LINK Engine)
    endif()

    add_executable(${target} ${TEST_SOURCES})
    target_link_libraries(${target} PRIVATE ${TEST_LINK})

    set(test_args ${TEST_ARGS})
    if(TEST_FRAMES)
        list(APPEND test_args ${TEST_FRAMES})
    endif()
    add_test(NAME ${target} COMMAND ${target} ${test_args})

    if(TEST_WINDOWED)
        set(labels "windowed")
    else()
        set(labels "headless")
    endif()
    if(TEST_EXTRA_LABELS)
        list(APPEND labels ${TEST_EXTRA_LABELS})
    endif()
    set_tests_properties(${target} PROPERTIES LABELS "${labels}")

    if(TEST_TIMEOUT)
        set_tests_properties(${target} PROPERTIES TIMEOUT ${TEST_TIMEOUT})
    endif()
    if(TEST_WORKING_DIRECTORY)
        set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY ${TEST_WORKING_DIRECTORY})
    endif()
    if(TEST_FOLDER)
        set_target_properties(${target} PROPERTIES FOLDER ${TEST_FOLDER})
    endif()
endfunction()

add_subdirectory(unit)
add_subdirectory(engine)
add_subdirectory(app)
