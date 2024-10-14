#ifndef FRAMEWORK_GO_GAMEOBJECT_INCLUDED
#define FRAMEWORK_GO_GAMEOBJECT_INCLUDED

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

#include <vector>
#include <memory>
#include "Framework/component/TransformComponent/TransformComponent.h"
#include "Reflection/reflection.h"

namespace Engine
{
    class Component;
    class Transfrom;

    class REFLECTION GameObject : public std::enable_shared_from_this<GameObject>
    {
    public:
        GameObject();
        virtual ~GameObject();

        virtual void Load();
        virtual void Unload();
        virtual void Tick(float dt);

        void AddComponent(std::shared_ptr<Component> component);

        REFLECTION const Transform & GetTransform() const;
        REFLECTION void SetTransform(const Transform& transform);
        REFLECTION Transform & GetTransformRef();
        REFLECTION Transform GetWorldTransform();

    public:
        REFLECTION std::weak_ptr<GameObject> m_parentGameObject;
        std::shared_ptr<TransformComponent> m_transformComponent;
        REFLECTION std::vector<std::shared_ptr<Component>> m_components {};

    protected:
        size_t m_id {};

    };
}

#pragma GCC diagnostic pop

#endif // FRAMEWORK_GO_GAMEOBJECT_INCLUDED
