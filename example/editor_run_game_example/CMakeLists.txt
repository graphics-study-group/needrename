include(${ANROREFL_PARSER_DIR}/parser.cmake)

get_filename_component(ProjectId ${CMAKE_CURRENT_LIST_DIR} NAME)
project(${ProjectId} LANGUAGES CXX)
add_executable(${ProjectId} main.cpp CustomComponent.cpp)

# There seems to be some bugs hiding, as only PUBLIC dependencies are scanned
# by the reflection parser.
target_link_libraries(${ProjectId} PUBLIC EngineEditor Engine)

set(all_include_dirs)
get_include_directories_for_target(${ProjectId} all_include_dirs)

add_reflection_parser(
    target_name meta_${ProjectId}
    reflection_search_files "${CMAKE_CURRENT_SOURCE_DIR}/CustomComponent.h"
    generated_code_dir "${CMAKE_CURRENT_LIST_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}"
    parent_projects meta_framework_generation meta_render_generation
)
add_dependencies(${ProjectId} meta_${ProjectId})
target_link_libraries(${ProjectId} PRIVATE meta_${ProjectId})

set_target_properties(${ProjectId} PROPERTIES FOLDER engine_examples)

if (EMSCRIPTEN)
    # A browser build produces a page to open, not a script to source.
    set_target_properties(${ProjectId} PROPERTIES SUFFIX ".html")

    # The engine reports missing data by throwing, and Emscripten disables exception catching by
    # default; without this the page aborts on the first thing it cannot find instead of saying so.
    target_compile_options(${ProjectId} PRIVATE -fexceptions)
    target_link_options(${ProjectId} PRIVATE -fexceptions -sNO_DISABLE_EXCEPTION_CATCHING=1)
    # The wait for the WebGPU adapter and device is a sleep loop, and the editor's main loop yields
    # once per frame; Asyncify is what makes those sleeps turn the page's event loop.
    target_link_options(${ProjectId} PRIVATE -sASYNCIFY -sASYNCIFY_STACK_SIZE=2097152)

    # The data root is preloaded into the module's filesystem, because a page has no directory next
    # to the executable to look in. `projects` is what the editor opens and copies to its scratch
    # project; `assets`, `builtin_assets` and `spirv` are what the engine loads.
    target_link_options(${ProjectId} PRIVATE
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/projects@/projects"
        "SHELL:--preload-file ${CMAKE_BINARY_DIR}/stage/$<CONFIG>/builtin_assets@/builtin_assets"
        "SHELL:--preload-file ${CMAKE_BINARY_DIR}/stage/$<CONFIG>/spirv@/spirv"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=FS"
    )
    target_compile_definitions(${ProjectId} PRIVATE ENGINE_DEV_DATA_ROOT_DIR="/")
endif()

