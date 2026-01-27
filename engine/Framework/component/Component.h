#ifndef FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_COMPONENT_INCLUDED

#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <memory>
#include <Framework/world/Handle.h>

namespace Engine {
    class GameObject;

    class REFL_SER_CLASS(REFL_WHITELIST) Component {
        REFL_SER_BODY(Component)

    protected:
        friend class WorldSystem;
        Component() = delete;
        Component(ObjectHandle parent_object);
    
    public:
        virtual ~Component() = default;

        /// @brief Initialize the component. Called when the parent GameObject before the first Tick after the
        /// GameObject is created.
        virtual void Init();
        /// @brief Called every frame.
        virtual void Tick();

        ComponentHandle GetHandle() const noexcept;
        GameObject *GetParentGameObject() const;

        bool operator==(const Component &other) const noexcept;

    public:
        ObjectHandle m_parentGameObject{0};

    protected:
        ComponentHandle m_handle{};
    };
} // namespace Engine


#endif // FRAMEWORK_COMPONENT_COMPONENT_INCLUDED
