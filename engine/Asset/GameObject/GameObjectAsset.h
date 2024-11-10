#ifndef ASSET_GAMEOBJECT_GAMEOBJECTASSET_INCLUDED
#define ASSET_GAMEOBJECT_GAMEOBJECTASSET_INCLUDED

#include <Asset/Asset.h>
#include <Framework/component/Component.h>
#include <meta_engine_reflection.hpp>

namespace Engine
{
    class REFL_SER_CLASS(REFL_WHITELIST) GameObjectAsset : public Asset
    {
        REFL_SER_BODY()
    public:
        REFL_SER_ENABLE GameObjectAsset(std::weak_ptr<AssetManager> manager);
        virtual ~GameObjectAsset();

        REFL_SER_ENABLE std::string m_name;
        REFL_SER_ENABLE std::vector<std::shared_ptr<Component>> m_components;
    };
}

#endif // ASSET_GAMEOBJECT_GAMEOBJECTASSET_INCLUDED
