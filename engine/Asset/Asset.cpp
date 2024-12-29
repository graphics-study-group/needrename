#include "Asset.h"
#include <Asset/AssetManager/AssetManager.h>
#include <MainClass.h>

namespace Engine
{
    Asset::Asset()
    {
        m_guid = MainClass::GetInstance()->GetAssetManager()->GenerateGUID();
    }

    Asset::~Asset()
    {
    }

    std::filesystem::path Asset::GetAssetPath()
    {
        return MainClass::GetInstance()->GetAssetManager()->GetAssetPath(m_guid);
    }

    std::filesystem::path Asset::GetMetaPath()
    {
        std::filesystem::path metaPath = MainClass::GetInstance()->GetAssetManager()->GetAssetPath(m_guid);
        metaPath.replace_extension(metaPath.extension().string() + ".asset");
        return metaPath;
    }

    void Asset::save_to_archive(Serialization::Archive& archive) const
    {
        Serialization::Json &json = *archive.m_cursor;
        json["Asset::m_guid"] = m_guid.toString();
        json["%type"] = Reflection::GetTypeFromObject(*this)->m_name;
    }

    void Asset::load_from_archive(Serialization::Archive& archive)
    {
        Serialization::Json &json = *archive.m_cursor;
        m_guid.fromString(json["Asset::m_guid"].get<std::string>());
        MainClass::GetInstance()->GetAssetManager()->AddToLoadingQueue(shared_from_this());
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
