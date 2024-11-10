#include "GameObjectAsset.h"

namespace Engine
{
    GameObjectAsset::GameObjectAsset(std::weak_ptr<AssetManager> manager) : Asset(manager)
    {
    }

    GameObjectAsset::~GameObjectAsset()
    {
    }
}
