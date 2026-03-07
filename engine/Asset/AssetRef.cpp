#include "AssetRef.h"
#include <Asset/Asset.h>
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>
#include <Reflection/serialization.h>

namespace Engine {
    AssetRef::AssetRef() {
    }

    AssetRef::AssetRef(GUID guid) : m_guid(guid) {
    }

    AssetRef::AssetRef(std::shared_ptr<Asset> asset) : m_asset(asset), m_guid(asset->GetGUID()) {
    }

    void AssetRef::save_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        if (m_guid.has_value()) {
            json = m_guid.value().toString();
        } else {
            json = nullptr;
        }
    }

    void AssetRef::load_from_archive(Serialization::Archive &archive) {
        Serialization::Json &json = *archive.m_cursor;
        if (json.is_null()) {
            m_guid.reset();
        } else {
            m_guid = GUID(json.get<std::string>());
        }
    }

    void AssetRef::Load() const {
        m_asset = MainClass::GetInstance()->GetAssetManager()->GetAsset(GetGUID());
    }

    void AssetRef::Unload() const {
        m_asset.reset();
    }

    bool AssetRef::IsLoaded() const {
        return m_asset != nullptr;
    }

    bool AssetRef::IsValid() const {
        return m_guid.has_value();
    }

    GUID AssetRef::GetGUID() const {
        return m_guid.value();
    }
} // namespace Engine
