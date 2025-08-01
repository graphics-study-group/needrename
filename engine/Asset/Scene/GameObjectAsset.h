#ifndef ASSET_SCENE_GAMEOBJECTASSET_H
#define ASSET_SCENE_GAMEOBJECTASSET_H

#include <memory>
#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>

namespace Engine
{
    class GameObject;

    class REFL_SER_CLASS(REFL_WHITELIST) GameObjectAsset : public Asset
    {
        REFL_SER_BODY(GameObjectAsset)
    public:
        REFL_ENABLE GameObjectAsset() = default;
        ~GameObjectAsset() = default;

        REFL_SER_ENABLE std::shared_ptr<GameObject> m_MainObject{};
    };
}

#endif // ASSET_SCENE_GAMEOBJECTASSET_H
