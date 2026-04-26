#include "ObjLoader.h"

#include "AssimpImportShared.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>

namespace Engine {
    ObjLoader::ObjLoader() {
        m_asset_manager = MainClass::GetInstance()->GetAssetManager();
        m_database = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    }

    void ObjLoader::LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project) {
        detail::AssimpImportOptions options{};
        options.source_name = "OBJ";
        detail::LoadResourceWithAssimp(path, path_in_project, m_asset_manager, m_database, options);
    }

    ImportResult ObjLoader::LoadObjInMemory(const std::filesystem::path &path) {
        detail::AssimpImportOptions options{};
        options.source_name = "OBJ";
        options.persist_assets = false;
        options.create_scene_asset = false;
        return detail::ImportWithAssimp(path, std::nullopt, m_asset_manager, m_database, options);
    }
} // namespace Engine
