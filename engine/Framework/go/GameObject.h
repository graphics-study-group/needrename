#ifndef FRAMEWORK_GO_GAMEOBJECT_INCLUDED
#define FRAMEWORK_GO_GAMEOBJECT_INCLUDED

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

#include <vector>
#include <memory>
#include "Framework/component/TransformComponent/TransformComponent.h"
#include "meta_engine_reflection.hpp"

namespace Engine
{
    class Component;
    class Transform;

    class REFL_SER_CLASS(REFL_WHITELIST) GameObject : public std::enable_shared_from_this<GameObject>
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE GameObject();
        virtual ~GameObject();

        virtual void Load();
        virtual void Unload();
        virtual void Tick(float dt);

        REFL_ENABLE void AddComponent(std::shared_ptr<Component> component);

        REFL_ENABLE const Transform & GetTransform() const;
        REFL_ENABLE void SetTransform(const Transform& transform);
        REFL_ENABLE Transform & GetTransformRef();
        REFL_ENABLE Transform GetWorldTransform();

    public:
        REFL_ENABLE std::weak_ptr<GameObject> m_parentGameObject;
        REFL_ENABLE std::shared_ptr<TransformComponent> m_transformComponent;
        REFL_ENABLE std::vector<std::shared_ptr<Component>> m_components {};

    protected:
        size_t m_id {};

    };
}

#pragma GCC diagnostic pop

#endif // FRAMEWORK_GO_GAMEOBJECT_INCLUDED
