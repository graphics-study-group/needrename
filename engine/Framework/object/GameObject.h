#ifndef FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
#define FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED

#include <Core/Math/Transform.h>
#include <Framework/world/WorldSystem.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <memory>
#include <string>
#include <vector>

namespace Engine {
    class Component;
    class TransformComponent;

    class REFL_SER_CLASS(REFL_BLACKLIST) GameObject {
        REFL_SER_BODY(GameObject)
    private:
        friend class WorldSystem;
        // GameObject must be created by WorldSystem's factory function
        GameObject() = default;

    public:
        virtual ~GameObject() = default;

        /// @brief Add a component of type T to the GameObject.
        /// @tparam T T must be derived from Component
        /// @tparam Args The arguments to be passed to the constructor of T
        /// @return The shared pointer to the created component
        template <typename T, typename... Args>
        ComponentHandle AddComponent(Args &&...args) {
            WorldSystem::GetInstance().CreateComponent<T>(m_handle, std::forward<Args>(args)...);
        }

        const Transform &GetTransform() const;
        void SetTransform(const Transform &transform);
        Transform &GetTransformRef();
        Transform GetWorldTransform();
        void SetParent(ObjectHandle parent);
        ObjectHandle GetHandle() const noexcept;

        bool operator==(const GameObject &other) const noexcept;

    public:
        std::string m_name{};
        ObjectHandle m_parentGameObject{0};
        std::vector<ObjectHandle> m_childGameObject{};

        ComponentHandle m_transformComponent{};
        std::vector<ComponentHandle> m_components{};

    protected:
        ObjectHandle m_handle{};
    };
} // namespace Engine

#endif // FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
