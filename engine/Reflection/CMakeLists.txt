file(GLOB_RECURSE SOURCE "./*.cpp")

add_library(EngineLibReflection 
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibReflection PRIVATE EngineLibHeaderInterface)

include(${REFLECTION_PARSER_DIR}/parser.cmake)
set(all_include_dirs)
get_include_directories_for_target(EngineLibHeaderInterface all_include_dirs)
filter_files_with_reflection_macros(HEADER REFLECTION_SEARCH_HEADERS)
add_reflection_parser(
    target_name meta_engine
    reflection_search_files "${REFLECTION_SEARCH_HEADERS}"
    generated_code_dir "${ENGINE_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${all_include_dirs}"
)
add_dependencies(EngineLibReflection meta_engine)
target_link_libraries(EngineLibReflection PRIVATE meta_engine)
