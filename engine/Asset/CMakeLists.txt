# Core asset infrastructure as its own DLL: Asset base class, AssetRef, AssetManager,
# AssetDatabase (interface + FileSystemDatabase), and the AssetRuntime service registry.
add_library(EngineAssetCore SHARED
    "Asset.cpp"
    "AssetRef.cpp"
    "AssetRuntime.cpp"
    "AssetReflectionRegistration.cpp"
    "AssetManager/AssetManager.cpp"
    "AssetDatabase/FileSystemDatabase.cpp"
)
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
set(ASSET_CORE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/Asset.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/AssetRef.h"
)
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

# Concrete asset types stay in engine.dll for now; they will be relocated to their
# owning modules (Render/Framework/Physics) in a follow-up change.
file(GLOB_RECURSE ASSET_TYPE_SOURCE
    "./Mesh/*.cpp"
    "./Material/*.cpp"
    "./Shader/*.cpp"
    "./Texture/*.cpp"
    "./Scene/*.cpp"
    "./Loader/*.cpp"
)

add_library(EngineLibAsset
    OBJECT
    ${ASSET_TYPE_SOURCE}
)
target_link_libraries(EngineLibAsset
    PRIVATE
    EngineLibHeaderInterface
    EngineLibExternalDependency
    EngineRhi
)
target_link_libraries(EngineLibAsset
    PUBLIC
    tinyobjloader
    stb
    ktx
    fastgltf::fastgltf
    tinyxml2
)
