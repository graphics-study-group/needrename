#include "Asset.h"
#include "GlobalSystem.h"
#include "AssetManager/AssetManager.h"

namespace Engine
{
    std::filesystem::path Asset::GetAssetPath()
    {
        return globalSystems.assetManager->GetAssetPath(m_guid);
    }

    std::filesystem::path Asset::GetMetaPath()
    {
        return globalSystems.assetManager->GetAssetPath(m_guid) / ".asset";
    }
}