#ifndef FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
#define FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED

#include <Core/Math/Transform.h>
#include <Framework/world/Handle.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <memory>
#include <string>
#include <vector>

namespace Engine {
    class Scene;
    class Component;
    class TransformComponent;

    /**
     * @brief GameObject is a container for Components.
     * GameObject represents the tree structure in the scene. It can have parent GameObject and child GameObject.
     * Every GameObject must have a TransformComponent.
     * GameObject can only be created by Scene's factory function.
     * GameObject's reference or pointer can only be obtained from Scene via ObjectHandle.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) GameObject {
        REFL_SER_BODY(GameObject)
    protected:
        friend class Scene;
        // GameObject must be created by Scene's factory function
        GameObject(Scene *scene);

    public:
        virtual ~GameObject() = default;
        GameObject(const GameObject &other) = delete;
        GameObject(GameObject &&other) = delete;
        GameObject &operator=(const GameObject &other) = delete;
        GameObject &operator=(GameObject &&other) = delete;

        /**
         * @brief Add a component of type T to the GameObject.
         * @tparam T T must be derived from Component
         * @return The reference to the created component
         */
        template <typename T>
        T &AddComponent() {
            static_assert(std::is_base_of_v<Component, T>, "T must be derived from Component");
            return static_cast<T &>(AddComponent(new T(*this)));
        }

        /**
         * @brief Get the Transform from the TransformComponent of the GameObject.
         * @return The reference to the Transform.
         */
        REFL_ENABLE const Transform &GetTransform() const;

        /**
         * @brief Set the Transform to the TransformComponent of the GameObject.
         * @param transform The new Transform.
         */
        REFL_ENABLE void SetTransform(const Transform &transform);

        /**
         * @brief Get the reference to the Transform in the TransformComponent of the GameObject.
         * @return The reference to the Transform.
         */
        REFL_ENABLE Transform &GetTransformRef();

        /**
         * @brief Get the world Transform of the GameObject.
         * This will traverse the parent GameObject tree to get the world Transform.
         * @return The reference to the world Transform.
         */
        REFL_ENABLE Transform GetWorldTransform();

        /**
         * @brief Set the parent GameObject of the GameObject.
         * @param parent The parent GameObject handle.
         */
        REFL_ENABLE void SetParent(ObjectHandle parent);

        /**
         * @brief Get the handle of the GameObject.
         * @return The handle of the GameObject.
         */
        REFL_ENABLE ObjectHandle GetHandle() const noexcept;

        /**
         * @brief Get the Scene that contains this GameObject.
         * @return The pointer to the Scene.
         */
        Scene *GetScene() const noexcept;

        /**
         * @brief Check if the GameObject's handle is the same as the other GameObject's handle.
         * @param other The other GameObject to compare.
         * @return True if the handles are the same, False otherwise.
         */
        bool operator==(const GameObject &other) const noexcept;

        /**
         * @brief Custom serialization function for GameObject.
         * Save the handle only.
         */
        void save_to_archive(Serialization::Archive &archive) const;

        /**
         * @brief Custom deserialization function for GameObject.
         * Load the handle only.
         */
        void load_from_archive(Serialization::Archive &archive);

    public:
        REFL_SER_ENABLE std::string m_name{};
        REFL_SER_ENABLE ObjectHandle m_parentGameObject{};
        REFL_SER_ENABLE std::vector<ObjectHandle> m_childGameObject{};

        REFL_SER_ENABLE ComponentHandle m_transformComponent{};
        REFL_SER_ENABLE std::vector<ComponentHandle> m_components{};

        /**
         * @brief Add a Component to the GameObject.
         * @param comp_ptr The Component pointer.
         * @return The reference to the created Component.
         */
        Component &AddComponent(Component *comp_ptr);

    private:
        Scene *m_scene{};
        ObjectHandle m_handle{};
    };
} // namespace Engine

#endif // FRAMEWORK_OBJECT_GAMEOBJECT_INCLUDED
