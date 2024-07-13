#ifndef LEVEL_H
#define LEVEL_H

#include <vector>
#include <memory>

namespace Engine
{
    class GameObject;

    class Level final
    {
    public:
        Level();
        ~Level();

        void tick(float dt);
        // void RenderEditor();
        void AddGameObject(std::shared_ptr<GameObject> gameObject);

    private:
        std::vector<std::shared_ptr<GameObject>> m_gameObjects;
    };
}

#endif // LEVEL_H