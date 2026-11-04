get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp)
# `PhysicsApp` is the engine's own rigid-body app: it owns the solver, the scene and the frame, so
# the example is a scene description plus a loop.
target_link_libraries(${ProjectId} PRIVATE Engine PhysicsApp)

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)

if (EMSCRIPTEN)
    # A browser build produces a page to open, not a script to source.
    set_target_properties(${ProjectId} PROPERTIES SUFFIX ".html")

    # The engine reports missing data by throwing, and Emscripten disables exception catching by
    # default; without this the page aborts on the first thing it cannot find instead of saying so.
    target_compile_options(${ProjectId} PRIVATE -fexceptions)
    target_link_options(${ProjectId} PRIVATE -fexceptions -sNO_DISABLE_EXCEPTION_CATCHING=1)
    # The wait for the WebGPU adapter and device is a sleep loop; Asyncify is what makes that sleep
    # yield to the event loop that answers it.
    target_link_options(${ProjectId} PRIVATE -sASYNCIFY -sASYNCIFY_STACK_SIZE=2097152)

    # The data root is preloaded into the module's filesystem, because a page has no directory next
    # to the executable to look in.
    target_link_options(${ProjectId} PRIVATE
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
        "SHELL:--preload-file ${CMAKE_BINARY_DIR}/stage/$<CONFIG>/builtin_assets@/builtin_assets"
        "SHELL:--preload-file ${CMAKE_BINARY_DIR}/stage/$<CONFIG>/spirv@/spirv"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/projects@/projects"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=FS"
    )
    target_compile_definitions(${ProjectId} PRIVATE ENGINE_DEV_DATA_ROOT_DIR="/")

    # The build used to trap here with `RuntimeError: null function` the moment this target linked
    # `PhysicsApp`: Emscripten built the app as a side module, whose symbols resolve to null in a
    # statically linked wasm image. `app/physics/CMakeLists.txt` builds a static archive for
    # Emscripten now, and this page runs the solver.
endif()
