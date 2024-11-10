#ifndef FRAMEWORK_LEVEL_LEVEL_H
#define FRAMEWORK_LEVEL_LEVEL_H

#include <vector>
#include <memory>

#include "Asset/Asset.h"

namespace Engine
{
    class GameObject;

    class REFL_SER_CLASS(REFL_WHITELIST) Level : public Asset
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE Level(std::weak_ptr <AssetManager> manager);
        ~Level();

        virtual void Load() override;
        virtual void Unload() override;
        virtual void Tick(float dt);

        REFL_ENABLE virtual void AddGameObject(std::shared_ptr<GameObject> gameObject);

    protected:
        std::vector<std::shared_ptr<GameObject>> m_gameObjects {};
    };
}

#endif // FRAMEWORK_LEVEL_LEVEL_H
