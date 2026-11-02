get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp)
target_link_libraries(${ProjectId} PRIVATE EngineDepSdl EngineDepWebgpu)

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)

if (EMSCRIPTEN)
    set_target_properties(${ProjectId} PROPERTIES SUFFIX ".html")
endif()
if (EMSCRIPTEN)
    # The adapter and device requests are answered by the browser on its own schedule, and a page
    # that spins on them never yields to the event loop that would answer. Asyncify is what lets
    # SDL_Delay inside the wait actually let the page breathe.
    target_link_options(${ProjectId} PRIVATE -sASYNCIFY)
endif()
