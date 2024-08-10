#ifndef FRMAEWORK_GO_GAMEOBJECT_H
#define FRMAEWORK_GO_GAMEOBJECT_H

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

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

        virtual void Load();
        virtual void Unload();
        virtual void Tick(float dt);

        void AddComponent(std::shared_ptr<Component> component);

        const Transform & GetTransform() const;
        Transform & GetTransformRef();

        Transform GetWorldTransform();
        void SetTransform(const Transform& transform);

    public:
        std::weak_ptr<GameObject> m_parentGameObject;
        std::shared_ptr<TransformComponent> m_transformComponent;

    protected:
        size_t m_id;

        std::vector<std::shared_ptr<Component>> m_components {};
    };
}

#pragma GCC diagnostic pop

#endif // FRMAEWORK_GO_GAMEOBJECT_H
