#ifndef FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
#define FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED

#include <vector>
#include <memory>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>

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
    private:
        friend class WorldSystem;
        // GameObject must be created by WorldSystem's factory function
        GameObject() = default;
    public:
        virtual ~GameObject() = default;

        /// @brief Add a component to the GameObject. Will set the component's parent to this GameObject.
        /// @param component The component to be added
        REFL_ENABLE void AddComponent(std::shared_ptr<Component> component);

        /// @brief Add a component of type T to the GameObject.
        /// @tparam T T must be derived from Component
        /// @tparam Args The arguments to be passed to the constructor of T
        /// @return The shared pointer to the created component
        template <typename T, typename... Args>
        std::shared_ptr<T> AddComponent(Args &&...args);

        REFL_ENABLE const Transform &GetTransform() const;
        REFL_ENABLE void SetTransform(const Transform &transform);
        REFL_ENABLE Transform &GetTransformRef();
        REFL_ENABLE Transform GetWorldTransform();
        REFL_ENABLE void SetParent(std::shared_ptr<GameObject> parent);

    public:
        REFL_SER_ENABLE std::weak_ptr<GameObject> m_parentGameObject{};
        REFL_SER_ENABLE std::vector<std::shared_ptr<GameObject>> m_childGameObject{};

        REFL_SER_ENABLE std::shared_ptr<TransformComponent> m_transformComponent{};
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
