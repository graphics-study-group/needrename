#ifndef ASSET_SCENE_LEVELASSET_H
#define ASSET_SCENE_LEVELASSET_H

#include <memory>
#include <vector>
#include <Asset/Asset.h>
#include <Reflection/macros.h>

namespace Engine
{
    class GameObject;

    class REFL_SER_CLASS(REFL_WHITELIST) LevelAsset : public Asset
    {
        REFL_SER_BODY(LevelAsset)
    public:
        REFL_ENABLE LevelAsset() = default;
        ~LevelAsset() = default;

        REFL_SER_ENABLE std::vector<std::shared_ptr<GameObject>> m_gameobjects{};
    };
}

#endif // ASSET_SCENE_LEVELASSET_H
