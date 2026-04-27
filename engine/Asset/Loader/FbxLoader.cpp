#include "FbxLoader.h"

#include "AssimpImportShared.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>

namespace Engine {
    FbxLoader::FbxLoader() {
        m_asset_manager = MainClass::GetInstance()->GetAssetManager();
        m_database = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    }

    void FbxLoader::LoadFbxResource(
        const std::filesystem::path &path,
        const std::filesystem::path &path_in_project,
        float scale_factor,
        bool convert_coordinate_system
    ) {
        detail::AssimpImportOptions options{};
        options.source_name = "FBX";
        options.scale_factor = scale_factor;
        options.convert_coordinate_system = convert_coordinate_system;
        detail::LoadResourceWithAssimp(path, path_in_project, m_asset_manager, m_database, options);
    }

    ImportResult FbxLoader::LoadFbxInMemory(
        const std::filesystem::path &path, float scale_factor, bool convert_coordinate_system
    ) {
        detail::AssimpImportOptions options{};
        options.source_name = "FBX";
        options.persist_assets = false;
        options.create_scene_asset = false;
        options.scale_factor = scale_factor;
        options.convert_coordinate_system = convert_coordinate_system;
        return detail::ImportWithAssimp(path, std::nullopt, m_asset_manager, m_database, options);
    }
} // namespace Engine
