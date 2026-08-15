file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE HEADER_PRIVATE "./*.hpp")

add_library(EngineLibRender
    OBJECT
    ${SOURCE}
)
target_link_libraries(EngineLibRender 
    PRIVATE 
    EngineLibHeaderInterface
    EngineLibExternalDependency
)
target_link_libraries(EngineLibRender
    PUBLIC
    ktx
)
target_precompile_headers(EngineLibRender PRIVATE "FullRenderSystem.h")

# Reflection: per-DLL registration for Render types.
file(GLOB_RECURSE RENDER_HEADERS "./*.h")
include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(RENDER_HEADERS RENDER_REFLECTION_HEADERS)

set(render_include_dirs)
get_include_directories_for_target(EngineLibHeaderInterface render_include_dirs)
get_include_directories_for_target(EngineLibExternalDependency render_include_dirs)

add_reflection_parser(
    target_name meta_render
    reflection_search_files "${RENDER_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${render_include_dirs}"
    parent_projects meta_core_generation meta_rhi_generation meta_asset_core_generation
)
add_dependencies(EngineLibRender meta_render)
target_link_libraries(EngineLibRender PRIVATE meta_render)
