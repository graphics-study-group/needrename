#ifndef FRAMEWORK_LEVEL_LEVEL_H
#define FRAMEWORK_LEVEL_LEVEL_H

#include <vector>
#include <memory>

#include "Asset/Asset.h"

namespace Engine
{
    class GameObject;

    class Level : public Asset
    {
    public:
        Level();
        ~Level();

        virtual void Load() override;
        virtual void Unload() override;
        virtual void Tick(float dt);

        virtual void AddGameObject(std::shared_ptr<GameObject> gameObject);

    protected:
        std::vector<std::shared_ptr<GameObject>> m_gameObjects;
    };
}

#endif // FRAMEWORK_LEVEL_LEVEL_H
