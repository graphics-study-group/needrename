#ifndef FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
#define FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED

#include <vector>
#include <memory>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Object/Object.h>
#include <meta_engine/reflection.hpp>

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class Component;
    class Transform;

    class REFL_SER_CLASS(REFL_WHITELIST) GameObject : public Object, public std::enable_shared_from_this<GameObject>
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
        REFL_ENABLE void SetParent(std::shared_ptr<GameObject> parent);

    public:
        REFL_ENABLE std::weak_ptr<GameObject> m_parentGameObject;
        REFL_ENABLE std::vector<std::shared_ptr<GameObject>> m_childGameObject;
        REFL_ENABLE std::shared_ptr<TransformComponent> m_transformComponent;
        REFL_ENABLE std::vector<std::shared_ptr<Component>> m_components {};
    };
}

#pragma GCC diagnostic pop

#endif // FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
