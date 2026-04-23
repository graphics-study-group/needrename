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
    void AssetManager::AddLoadedAsset(std::unique_ptr<Asset> asset) {
        m_loaded_assets.emplace(asset->GetGUID(), std::move(asset));
    }

    void AssetManager::AddToLoadingQueue(const GUID &guid) {
        if (m_in_loading_queue.contains(guid)) {
            return;
        }
        m_in_loading_queue.insert(guid);
        m_loading_queue.push(guid);
    }

    void AssetManager::LoadAssetsInQueue() {
        while (!m_loading_queue.empty()) {
            auto guid = m_loading_queue.front();
            if (IsAssetLoaded(guid)) {
                m_in_loading_queue.erase(guid);
                m_loading_queue.pop();
                continue;
            }

            Serialization::Archive archive;
            MainClass::GetInstance()->GetAssetDatabase()->LoadArchive(archive, guid);
            archive.prepare_load();

            auto asset_type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
            assert(asset_type->IsReflectable());
            // TODO: asset memory management
            auto var = asset_type->CreateInstance(Serialization::SerializationMarker{});
            auto asset_ptr = std::unique_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
            var.SetNeedFree(false);
            asset_ptr->load_asset_from_archive(archive);
            m_loaded_assets.emplace(guid, std::move(asset_ptr));

            m_loading_queue.pop();
        }
    }

    Asset *AssetManager::LoadAssetImmediately(const GUID &guid) {
        if (IsAssetLoaded(guid)) {
            return m_loaded_assets[guid].get();
        }
        Serialization::Archive archive;
        MainClass::GetInstance()->GetAssetDatabase()->LoadArchive(archive, guid);
        auto type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
        assert(type->IsReflectable());
        auto var = type->CreateInstance(Serialization::SerializationMarker{});
        std::unique_ptr<Asset> new_asset = std::unique_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
        var.SetNeedFree(false);
        archive.prepare_load();
        new_asset->load_asset_from_archive(archive);
        GUID new_asset_guid = new_asset->GetGUID();
        m_loaded_assets.emplace(new_asset_guid, std::move(new_asset));
        return GetAsset(new_asset_guid);
    }

    Asset *AssetManager::GetAsset(const GUID &guid) {
        auto it = m_loaded_assets.find(guid);
        if (it == m_loaded_assets.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    bool AssetManager::IsAssetLoaded(const GUID &guid) {
        return m_loaded_assets.find(guid) != m_loaded_assets.end();
    }

    void AssetManager::UnloadAsset(const GUID &guid) {
        if (IsAssetLoaded(guid)) {
            m_loaded_assets.erase(guid);
        }
    }

    void AssetManager::UnloadUnusedAssets() {
        bool has_unused_assets = true;
        while (has_unused_assets) {
            has_unused_assets = false;
            for (auto it = m_asset_ref_count.begin(); it != m_asset_ref_count.end();) {
                if (it->second == 0) {
                    UnloadAsset(it->first);
                    it = m_asset_ref_count.erase(it);
                    has_unused_assets = true;
                } else {
                    ++it;
                }
            }
        }
    }

    void AssetManager::IncrementRefCount(const GUID &guid) {
        ++m_asset_ref_count[guid];
    }

    void AssetManager::DecrementRefCount(const GUID &guid) {
        assert(m_asset_ref_count.contains(guid));
        --m_asset_ref_count[guid];
    }
} // namespace Engine
