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
    void AssetManager::AddToLoadingQueue(std::shared_ptr<AssetRef> asset_ref) {
        m_loading_queue.push(asset_ref);
    }

    void AssetManager::LoadAssetsInQueue() {
        while (!m_loading_queue.empty()) {
            auto asset_ref = m_loading_queue.front();

            Serialization::Archive archive;
            MainClass::GetInstance()->GetAssetDatabase()->LoadArchive(archive, asset_ref->GetGUID());
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
        Serialization::Archive archive;
        MainClass::GetInstance()->GetAssetDatabase()->LoadArchive(archive, guid);
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
