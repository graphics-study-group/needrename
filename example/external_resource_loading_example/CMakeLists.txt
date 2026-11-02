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
if (EMSCRIPTEN)
    # The engine reports missing data by throwing, and Emscripten disables exception catching by
    # default; without this the page aborts on the first thing it cannot find instead of saying so.
    target_compile_options(${ProjectId} PRIVATE -fexceptions)
    target_link_options(${ProjectId} PRIVATE -fexceptions -sNO_DISABLE_EXCEPTION_CATCHING=1)
    # The WebGPU device and adapter are answered by the browser on its own schedule, and the wait
    # for them is a sleep loop. Asyncify is what makes that sleep yield to the event loop that
    # would answer; without it the page hangs at "creating DeviceInterface".
    target_link_options(${ProjectId} PRIVATE -sASYNCIFY)

    # The engine resolves its data root by looking for directories next to the executable. In a
    # browser there is no such place, so the tree it needs is packed into the module's virtual
    # filesystem and the root is pointed at where it lands.
    target_link_options(${ProjectId} PRIVATE
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
        "SHELL:--preload-file ${CMAKE_BINARY_DIR}/stage/$<CONFIG>/builtin_assets@/builtin_assets"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/projects@/projects"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=FS"
    )
    target_compile_definitions(${ProjectId} PRIVATE ENGINE_DEV_DATA_ROOT_DIR="/")
endif()



