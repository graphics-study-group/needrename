#include "Asset.h"
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>
#include <Reflection/serialization.h>

namespace Engine {
    Asset::Asset() {
        m_guid = MainClass::GetInstance()->GetAssetManager()->GenerateGUID();
    }

    Asset::~Asset() {
    }

    std::filesystem::path Asset::GetAssetPath() {
        return MainClass::GetInstance()->GetAssetManager()->GetAssetPath(m_guid);
    }

    void Asset::save_to_archive(Serialization::Archive &archive) const {
        throw std::runtime_error("Asset serialization is not allowed. Use AssetRef instead.");
    }

    void Asset::load_from_archive(Serialization::Archive &archive) {
        throw std::runtime_error("Asset serialization is not allowed. Use AssetRef instead.");
    }

    void Asset::save_asset_to_archive(Serialization::Archive &archive) const {
        _SERIALIZATION_SAVE_(archive);
        Serialization::Json &json = *archive.m_cursor;
        json["Asset::m_guid"] = m_guid.toString();
    }

    void Asset::load_asset_from_archive(Serialization::Archive &archive) {
        _SERIALIZATION_LOAD_(archive);
        Serialization::Json &json = *archive.m_cursor;
        m_guid.fromString(json["Asset::m_guid"].get<std::string>());
    }

    GUID Asset::GetGUID() const {
        return m_guid;
    }
} // namespace Engine
