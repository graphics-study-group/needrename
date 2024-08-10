#include "Asset.h"
#include "GlobalSystem.h"
#include "AssetManager/AssetManager.h"

namespace Engine
{
    Asset::Asset()
    {
    }

    Asset::~Asset()
    {
    }

    std::filesystem::path Asset::GetAssetPath()
    {
        return globalSystems.assetManager->GetAssetPath(m_guid);
    }

    std::filesystem::path Asset::GetMetaPath()
    {
        std::filesystem::path metaPath = globalSystems.assetManager->GetAssetPath(m_guid);
        metaPath.replace_extension(metaPath.extension().string() + ".asset");
        return metaPath;
    }
}