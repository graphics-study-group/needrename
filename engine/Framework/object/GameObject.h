#ifndef FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
#define FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED

#include <Core/Math/Transform.h>
#include <Framework/world/Scene.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <memory>
#include <string>
#include <vector>

namespace Engine {
    class Component;
    class TransformComponent;

    class REFL_SER_CLASS(REFL_WHITELIST) GameObject {
        REFL_SER_BODY(GameObject)
    protected:
        friend class Scene;
        friend class Component;
        // GameObject must be created by Scene's factory function
        GameObject(Scene *scene);

    public:
        virtual ~GameObject() = default;
        GameObject(const GameObject &other) = delete;
        GameObject(GameObject &&other) = delete;
        GameObject &operator=(const GameObject &other) = delete;
        GameObject &operator=(GameObject &&other) = delete;

        /// @brief Add a component of type T to the GameObject.
        /// @tparam T T must be derived from Component
        /// @return The reference to the created component
        template <typename T>
        T &AddComponent() {
            static_assert(std::is_base_of_v<Component, T>, "T must be derived from Component");
            return m_scene->CreateComponent<T>(*this);
        }

        REFL_ENABLE const Transform &GetTransform() const;
        REFL_ENABLE void SetTransform(const Transform &transform);
        REFL_ENABLE Transform &GetTransformRef();
        REFL_ENABLE Transform GetWorldTransform();
        REFL_ENABLE void SetParent(ObjectHandle parent);
        REFL_ENABLE ObjectHandle GetHandle() const noexcept;

        bool operator==(const GameObject &other) const noexcept;

    public:
        REFL_SER_ENABLE std::string m_name{};
        REFL_SER_ENABLE ObjectHandle m_parentGameObject{};
        REFL_SER_ENABLE std::vector<ObjectHandle> m_childGameObject{};

        REFL_SER_ENABLE ComponentHandle m_transformComponent{};
        REFL_SER_ENABLE std::vector<ComponentHandle> m_components{};

    protected:
        Scene *m_scene{};
        ObjectHandle m_handle{};
    };
} // namespace Engine

#endif // FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
