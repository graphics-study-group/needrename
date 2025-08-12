#include "AssetRef.h"
#include <Asset/Asset.h>
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>
#include <Reflection/serialization.h>

namespace Engine {
    AssetRef::AssetRef() {
        m_guid = MainClass::GetInstance()->GetAssetManager()->GenerateGUID();
    }

    AssetRef::AssetRef(GUID guid) : m_guid(guid) {
    }

    AssetRef::AssetRef(std::shared_ptr<Asset> asset) : m_asset(asset), m_guid(asset->GetGUID()) {
    }

    void AssetRef::save_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        json["m_guid"] = m_guid.toString();
        json["%type"] = Reflection::GetTypeFromObject(*this)->GetName();
    }

    void AssetRef::load_from_archive(Serialization::Archive &archive) {
        Serialization::Json &json = *archive.m_cursor;
        m_guid.fromString(json["m_guid"].get<std::string>());
        MainClass::GetInstance()->GetAssetManager()->AddToLoadingQueue(shared_from_this());
    }

    void AssetRef::Unload() {
        m_asset = nullptr;
    }

    bool AssetRef::IsValid() {
        return m_asset != nullptr;
    }

    GUID AssetRef::GetGUID() {
        return m_guid;
    }
} // namespace Engine
