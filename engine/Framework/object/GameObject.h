#ifndef FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
#define FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED

#include <vector>
#include <memory>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <meta_engine/reflection.hpp>

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class Component;
    class Transform;
    class WorldSystem;

    class REFL_SER_CLASS(REFL_WHITELIST) GameObject : public std::enable_shared_from_this<GameObject>
    {
        REFL_SER_BODY(GameObject)
    public:
        // GameObject must be created by WorldSystem's factory function
        GameObject() = delete;
        // No use. Just make sure the GameObject must be created by WorldSystem's factory function
        GameObject(const WorldSystem *marker);
        virtual ~GameObject();

        REFL_ENABLE void AddComponent(std::shared_ptr<Component> component);
        template <typename T, typename... Args>
        std::shared_ptr<T> AddComponent(Args &&...args);

        REFL_ENABLE const Transform &GetTransform() const;
        REFL_ENABLE void SetTransform(const Transform &transform);
        REFL_ENABLE Transform &GetTransformRef();
        REFL_ENABLE Transform GetWorldTransform();
        REFL_ENABLE void SetParent(std::shared_ptr<GameObject> parent);

    public:
        REFL_SER_ENABLE std::weak_ptr<GameObject> m_parentGameObject{};
        REFL_SER_ENABLE std::vector<std::shared_ptr<GameObject>> m_childGameObject;

        REFL_SER_ENABLE std::shared_ptr<TransformComponent> m_transformComponent;
        REFL_SER_ENABLE std::vector<std::shared_ptr<Component>> m_components{};
    };

    template <typename T, typename... Args>
    std::shared_ptr<T> GameObject::AddComponent(Args &&...args)
    {
        static_assert(std::is_base_of<Component, T>::value, "T must be derived from Component");
        auto component = std::make_shared<T>(weak_from_this(), std::forward<Args>(args)...);
        AddComponent(component);
        return component;
    }
}

#pragma GCC diagnostic pop

#endif // FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
