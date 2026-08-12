file(GLOB_RECURSE SOURCE "./*.cpp")
file(GLOB_RECURSE ASSET_CORE_HEADERS "./*.h")

add_library(EngineAssetCore SHARED ${SOURCE})
target_compile_definitions(EngineAssetCore PRIVATE ASSET_CORE_DLL_EXPORTS)
target_link_libraries(EngineAssetCore
    PUBLIC
    EngineLibHeaderInterface
    AnnoRefl
    PRIVATE
    EngineCore
)

# Reflection: per-DLL registration for Asset and AssetRef.
include(${ANROREFL_PARSER_DIR}/parser.cmake)
filter_files_with_reflection_macros(ASSET_CORE_HEADERS ASSET_CORE_REFLECTION_HEADERS)

set(asset_core_include_dirs)
get_include_directories_for_target(EngineAssetCore asset_core_include_dirs)

add_reflection_parser(
    target_name meta_asset_core
    reflection_search_files "${ASSET_CORE_REFLECTION_HEADERS}"
    generated_code_dir "${CMAKE_CURRENT_SOURCE_DIR}/__generated__"
    reflection_search_include_dirs "${asset_core_include_dirs}"
)
add_dependencies(EngineAssetCore meta_asset_core)
target_link_libraries(EngineAssetCore PRIVATE meta_asset_core)
