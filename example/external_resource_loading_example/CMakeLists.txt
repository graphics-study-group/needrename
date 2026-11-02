get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp)
target_link_libraries(${ProjectId} PRIVATE Engine)

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)
# A browser build produces a page to open, not a script to source. The suffix is what tells
# Emscripten to emit the HTML shell alongside the module.
if (EMSCRIPTEN)
    set_target_properties(${ProjectId} PROPERTIES SUFFIX ".html")
endif()
