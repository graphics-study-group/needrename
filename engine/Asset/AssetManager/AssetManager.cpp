#include "AssetManager.h"

#include "Asset/Loader/ObjLoader.h"
#include <Asset/Asset.h>
#include <Asset/AssetDatabase/AssetDatabase.h>
#include <Asset/AssetRef.h>
#include <MainClass.h>
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Engine {
    void AssetManager::LoadBuiltinAssets() {
        for (const auto &[relative_path, guid] : MainClass::GetInstance()->GetAssetDatabase()->ListAssets("~", true)) {
            AddAsset(guid, relative_path);
        }
    }

    void AssetManager::LoadProject() {
        for (const auto &[relative_path, guid] : MainClass::GetInstance()->GetAssetDatabase()->ListAssets({}, true)) {
            AddAsset(guid, relative_path);
        }
    }

    void AssetManager::ImportExternalResource(
        std::filesystem::path resourcePath, std::filesystem::path path_in_project
    ) {
        // if (!std::filesystem::exists(GetAssetsDirectory() / path_in_project))
        //     std::filesystem::create_directory(GetAssetsDirectory() / path_in_project);
        // std::string extension = resourcePath.extension().string();
        // if (extension == ".obj") {
        //     ObjLoader loader;
        //     loader.LoadObjResource(resourcePath, path_in_project);
        // } else {
        //     throw std::runtime_error("Unsupported file format");
        // }
    }

    std::filesystem::path AssetManager::GetAssetPath(GUID guid) const {
        auto it = m_assets_map.find(guid);
        if (it != m_assets_map.end()) {
            return it->second;
        } else {
            throw std::runtime_error("Asset not found");
        }
    }

    std::filesystem::path AssetManager::GetAssetPath(const std::shared_ptr<Asset> &asset) const {
        return GetAssetPath(asset->GetGUID());
    }

    std::shared_ptr<AssetRef> AssetManager::GetNewAssetRef(const std::filesystem::path &path) {
        std::string path_str = path.lexically_normal().generic_string();
        if (m_path_to_guid.find(path_str) == m_path_to_guid.end()) return nullptr;
        return std::make_shared<AssetRef>(m_path_to_guid[path_str]);
    }

    void AssetManager::AddAsset(const GUID &guid, const std::filesystem::path &path) {
        if (m_assets_map.find(guid) != m_assets_map.end()) throw std::runtime_error("asset GUID already exists");
        std::string path_str = path.lexically_normal().generic_string();
        m_assets_map[guid] = path_str;
        m_path_to_guid[path_str] = guid;
    }

    void AssetManager::AddToLoadingQueue(std::shared_ptr<AssetRef> asset_ref) {
        m_loading_queue.push(asset_ref);
    }

    void AssetManager::LoadAssetsInQueue() {
        while (!m_loading_queue.empty()) {
            auto asset_ref = m_loading_queue.front();
            auto path = GetAssetPath(asset_ref->GetGUID());

            Serialization::Archive archive;
            MainClass::GetInstance()->GetAssetDatabase()->LoadArchive(archive, path);
            archive.prepare_load();

            auto asset_type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
            assert(asset_type->IsReflectable());
            // TODO: asset memory management
            auto var = asset_type->CreateInstance(Serialization::SerializationMarker{});
            auto asset_ptr = std::shared_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
            var.MarkNeedFree(false);
            asset_ptr->load_asset_from_archive(archive);
            asset_ref->m_asset = asset_ptr;

            m_loading_queue.pop();
        }
    }

    std::shared_ptr<Asset> AssetManager::LoadAssetImmediately(const GUID &guid) {
        auto path = GetAssetPath(guid);
        Serialization::Archive archive;
        MainClass::GetInstance()->GetAssetDatabase()->LoadArchive(archive, path);
        auto type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
        assert(type->IsReflectable());
        auto var = type->CreateInstance(Serialization::SerializationMarker{});
        std::shared_ptr<Asset> ret = std::shared_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
        var.MarkNeedFree(false);
        archive.prepare_load();
        ret->load_asset_from_archive(archive);
        return ret;
    }

    void AssetManager::LoadAssetImmediately(std::shared_ptr<AssetRef> asset_ref) {
        auto guid = asset_ref->GetGUID();
        asset_ref->m_asset = LoadAssetImmediately(guid);
    }
} // namespace Engine
