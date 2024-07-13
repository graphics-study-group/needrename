#ifndef COMPONENT_H
#define COMPONENT_H

#include <memory>

namespace Engine
{
    class GameObject;

    class Component
    {
    public:
        Component(std::weak_ptr<GameObject> gameObject) : m_parentGameObject(gameObject) {}
        virtual ~Component() = default;

        virtual void tick(float dt) = 0;

    protected:
        std::weak_ptr<GameObject> m_parentGameObject;
    };
}
#endif // COMPONENT_H