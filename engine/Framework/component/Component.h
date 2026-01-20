#ifndef FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_COMPONENT_INCLUDED

#include <Framework/object/GameObject.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <memory>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine {
    class GameObject;

    class REFL_SER_CLASS(REFL_WHITELIST) Component : public std::enable_shared_from_this<Component> {
        REFL_SER_BODY(Component)
    public:
        Component() = delete;
        REFL_ENABLE Component(std::weak_ptr<GameObject> gameObject);
        virtual ~Component() = default;

        /// @brief Initialize the component. Called when the parent GameObject before the first Tick after the
        /// GameObject is created.
        virtual void Init();

        /// @brief Called every frame.
        virtual void Tick();

        ObjectID GetID() const noexcept;

        bool operator==(const Component &other) const noexcept;

    public:
        REFL_SER_ENABLE std::weak_ptr<GameObject> m_parentGameObject{};

    protected:
        friend class WorldSystem;
        ObjectID m_id{};
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
