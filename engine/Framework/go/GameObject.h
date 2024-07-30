#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <vector>
#include <memory>
#include "Framework/component/TransformComponent/TransformComponent.h"

namespace Engine
{
    class Component;
    class Transfrom;

    class GameObject : public std::enable_shared_from_this<GameObject>
    {
    public:
        GameObject();
        virtual ~GameObject();

        virtual void Tick(float dt);

        void AddComponent(std::shared_ptr<Component> component);
        Transform GetTransform();
        Transform GetWorldTransform();
        void SetTransform(const Transform& transform);

    public:
        std::weak_ptr<GameObject> m_parentGameObject;
        std::shared_ptr<TransformComponent> m_transformComponent;

    protected:
        size_t m_id;

        std::vector<std::shared_ptr<Component>> m_components;
    };
}

#endif // GAMEOBJECT_H
