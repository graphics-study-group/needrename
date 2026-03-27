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
    void AssetManager::AddToLoadingQueue(const GUID &guid) {
        m_loading_queue.push(guid);
    }

    void AssetManager::LoadAssetsInQueue() {
        while (!m_loading_queue.empty()) {
            auto guid = m_loading_queue.front();
            if (IsAssetLoaded(guid)) {
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
            auto asset_ptr = std::shared_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
            var.SetNeedFree(false);
            asset_ptr->load_asset_from_archive(archive);
            m_loaded_assets[guid] = asset_ptr;

            m_loading_queue.pop();
        }
    }

    std::shared_ptr<Asset> AssetManager::LoadAssetImmediately(const GUID &guid) {
        if (IsAssetLoaded(guid)) {
            return m_loaded_assets[guid];
        }
        Serialization::Archive archive;
        MainClass::GetInstance()->GetAssetDatabase()->LoadArchive(archive, guid);
        auto type = Reflection::GetType(archive.GetMainDataProperty("%type").get<std::string>());
        assert(type->IsReflectable());
        auto var = type->CreateInstance(Serialization::SerializationMarker{});
        std::shared_ptr<Asset> ret = std::shared_ptr<Asset>(static_cast<Asset *>(var.GetDataPtr()));
        var.SetNeedFree(false);
        archive.prepare_load();
        ret->load_asset_from_archive(archive);
        m_loaded_assets[guid] = ret;
        return ret;
    }

    std::shared_ptr<Asset> AssetManager::GetAsset(const GUID &guid, bool load_if_not_loaded) {
        if (m_loaded_assets.find(guid) == m_loaded_assets.end()) {
            if (load_if_not_loaded) {
                m_loaded_assets[guid] = LoadAssetImmediately(guid);
            } else {
                return nullptr;
            }
        }
        return m_loaded_assets[guid];
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
            for (auto it = m_loaded_assets.begin(); it != m_loaded_assets.end();) {
                if (it->second.use_count() == 1) {
                    it = m_loaded_assets.erase(it);
                    has_unused_assets = true;
                } else {
                    ++it;
                }
            }
        }
    }
} // namespace Engine
