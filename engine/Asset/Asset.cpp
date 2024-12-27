#include "Asset.h"
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>

namespace Engine
{
    Asset::Asset()
    {
        m_manager = MainClass::GetInstance()->GetAssetManager();
        m_guid = m_manager.lock()->GenerateGUID();
    }

    Asset::~Asset()
    {
    }

    std::filesystem::path Asset::GetAssetPath()
    {
        return m_manager.lock()->GetAssetPath(m_guid);
    }

    std::filesystem::path Asset::GetMetaPath()
    {
        std::filesystem::path metaPath = m_manager.lock()->GetAssetPath(m_guid);
        metaPath.replace_extension(metaPath.extension().string() + ".asset");
        return metaPath;
    }

    void Asset::save_to_archive(Serialization::Archive& archive) const
    {
        Serialization::Json &json = *archive.m_cursor;
        json = m_guid.toString();
        // TODO: use asset manager queue
    }

    void Asset::load_from_archive(Serialization::Archive& archive)
    {
        Serialization::Json &json = *archive.m_cursor;
        m_guid.fromString(json.get<std::string>());
        // TODO: use asset manager queue
    }

    void Asset::save_asset_to_archive(Serialization::Archive& archive) const
    {
        __serialization_save__(archive);
        Serialization::Json &json = *archive.m_cursor;
        json["Asset::m_guid"] = m_guid.toString();
    }

    void Asset::load_asset_from_archive(Serialization::Archive& archive)
    {
        __serialization_load__(archive);
        Serialization::Json &json = *archive.m_cursor;
        m_guid.fromString(json["Asset::m_guid"].get<std::string>());
        SetValid(true);
    }

    void Asset::Unload()
    {
        SetValid(false);
    }
}
