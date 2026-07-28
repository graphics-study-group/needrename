#include "Asset.h"
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>
#include <AnnoRefl/serialization.h>

namespace Engine {
    Asset::Asset() {
        m_guid = GUID::Random();
    }

    Asset::~Asset() {
    }

    void Asset::save_to_archive(AnnoRefl::Archive &) const {
        throw std::runtime_error("Asset serialization is not allowed. Use AssetRef instead.");
    }

    void Asset::load_from_archive(AnnoRefl::Archive &) {
        throw std::runtime_error("Asset serialization is not allowed. Use AssetRef instead.");
    }

    void Asset::save_asset_to_archive(AnnoRefl::Archive &archive) const {
        _SERIALIZATION_SAVE_(archive);
        AnnoRefl::Json &json = *archive.m_cursor;
        json["Asset::m_guid"] = m_guid.string();
    }

    void Asset::load_asset_from_archive(AnnoRefl::Archive &archive) {
        _SERIALIZATION_LOAD_(archive);
        AnnoRefl::Json &json = *archive.m_cursor;
        m_guid = GUID(json["Asset::m_guid"].get<std::string>());
    }

    GUID Asset::GetGUID() const {
        return m_guid;
    }
} // namespace Engine

#include "__generated__/Asset.h.inc"
