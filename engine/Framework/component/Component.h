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
        virtual ~Component();

        virtual void tick(float dt);

    protected:
        std::weak_ptr<GameObject> m_parentGameObject;
    };
}
#endif // COMPONENT_H