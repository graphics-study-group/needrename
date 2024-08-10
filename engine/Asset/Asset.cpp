#include "Asset.h"
#include "AssetManager/AssetManager.h"

namespace Engine
{
    Asset::Asset(std::weak_ptr <AssetManager> manager) : m_manager(manager)
    {
        m_guid = m_manager.lock()->GenerateGUID();
    }

    Asset::~Asset()
    {
    }

    void Asset::Load()
    {
        m_valid = true;
    }

    void Asset::Unload()
    {
        m_valid = false;
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
}