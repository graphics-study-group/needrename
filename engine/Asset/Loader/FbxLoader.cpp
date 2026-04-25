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

    void FbxLoader::LoadFbxResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project) {
        detail::AssimpImportOptions options{};
        options.enable_fbx_compat = true;
        options.source_name = "FBX";
        detail::LoadResourceWithAssimp(path, path_in_project, m_asset_manager, m_database, options);
    }
} // namespace Engine
