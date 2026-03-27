#ifndef FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_COMPONENT_INCLUDED

#include <Framework/object/GameObject.h>
#include <Framework/world/Handle.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <memory>

namespace Engine {
    class Scene;

    /**
     * @brief Components contains the actual functional logic and data of the game.
     * Component is attached to GameObjects, and is responsible for the behavior of the GameObject.
     * Component can only be created via Scene's factory function.
     * Component's reference or pointer can only be obtained from Scene via ComponentHandle.
     *
     * Each Component must have Init() and Tick() functions.
     * Init() is called when the parent GameObject is added to a running scene.
     * Tick() is called every frame.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) Component {
        REFL_SER_BODY(Component)

    protected:
        friend class Scene;
        friend class SceneAsset;
        Component() = delete;
        Component(const GameObject &parent);

    public:
        virtual ~Component() = default;
        Component(const Component &other) = delete;
        Component(Component &&other) = delete;
        Component &operator=(const Component &other) = delete;
        Component &operator=(Component &&other) = delete;

        /**
         * @brief Initialize the component.
         * Called when the parent GameObject is added to a running scene.
         */
        REFL_ENABLE virtual void Init();

        /**
         * @brief Called every frame.
         */
        REFL_ENABLE virtual void Tick();

        /**
         * @brief Get the Component handle.
         * @return The Component handle.
         */
        REFL_ENABLE ComponentHandle GetHandle() const noexcept;

        /**
         * @brief Get the parent GameObject.
         * @return The parent GameObject.
         */
        GameObject *GetParentGameObject() const noexcept;

        /**
         * @brief Get the scene the Component is attached to.
         * @return The scene the Component is attached to.
         */
        Scene *GetScene() const noexcept;

        /**
         * @brief Check if Component handles are equal.
         * @param other The other Component.
         * @return True if the Component handles are equal, False otherwise.
         */
        bool operator==(const Component &other) const noexcept;

        /**
         * @brief Custom serialization function.
         * Save the handle and other automatically serialized fields in DerivedClass.
         */
        void save_to_archive(Serialization::Archive &archive) const;

        /**
         * @brief Custom deserialization function.
         * Load the handle and other automatically serialized fields in DerivedClass.
         */
        void load_from_archive(Serialization::Archive &archive);

    public:
        REFL_SER_ENABLE ObjectHandle m_parentGameObject{};

    private:
        Scene *m_scene{};
        ComponentHandle m_handle{};
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
