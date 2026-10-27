message("COMPILE engine editor")
file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER "./*.h")

list(FILTER SOURCE EXCLUDE REGEX "__generated__/.*")
list(FILTER HEADER EXCLUDE REGEX "__generated__/.*")

add_library(EngineEditor SHARED ${SOURCE})
if(MSVC)
    # The editor code has no dllexport annotations; export all symbols on
    # Windows so an import library is produced (Linux .so exports by default).
    set_target_properties(EngineEditor PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
target_include_directories(EngineEditor PUBLIC ${ENGINE_EDITOR_DIR})
target_link_libraries(EngineEditor PUBLIC Engine)

include(${ANROREFL_PARSER_DIR}/parser.cmake)
set(all_include_dirs)
get_include_directories_for_target(EngineEditor all_include_dirs)
filter_files_with_reflection_macros(HEADER REFLECTION_SEARCH_HEADERS)

add_reflection_parser(
    target_name meta_editor
    reflection_search_files "${REFLECTION_SEARCH_HEADERS}"
    generated_code_dir "${ENGINE_EDITOR_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}"
)
add_dependencies(EngineEditor meta_editor)
target_link_libraries(EngineEditor PRIVATE meta_editor)

