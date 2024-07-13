#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <vector>
#include <memory>

namespace Engine
{
    class Component;

    class GameObject
    {
    public:
        GameObject();
        virtual ~GameObject();

        virtual void tick(float dt);

        void AddComponent(std::shared_ptr<Component> component);

    private:
        size_t m_id;
        std::vector<std::shared_ptr<Component>> m_components;
    };
}

#endif // GAMEOBJECT_H