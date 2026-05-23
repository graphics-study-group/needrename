#ifndef FRAMEWORK_COMPONENT_PHYSICS_COLLISIONSHAPECOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_PHYSICS_COLLISIONSHAPECOMPONENT_INCLUDED

#include <Core/Math/Transform.h>
#include <Framework/component/Component.h>
#include <Physics/PhysicsScene.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>

namespace Engine {
    /**
     * @brief Physics collision shape component.
     *
     * This component stores editable collision shape parameters and registers
     * one shape record into PhysicsScene during Awake.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) CollisionShapeComponent : public Component {
        REFL_SER_BODY(CollisionShapeComponent)
    public:
        /**
         * @brief Construct a collision shape component.
         *
         * @param parent Parent game object.
         */
        REFL_ENABLE explicit CollisionShapeComponent(const GameObject &parent);

        /**
         * @brief Destroy the collision shape component.
         *
         * The destructor attempts to unregister this shape from PhysicsScene.
         */
        virtual ~CollisionShapeComponent();

        /**
         * @brief Register this shape into PhysicsScene.
         *
         * This fills shape feature data and tries to attach to an ancestor
         * rigid body if one is already registered.
         */
        void Awake() override;

        /**
         * @brief Check whether this shape has a valid physics registration.
         *
         * @return True if the shape index is valid in PhysicsScene.
         */
        bool IsRegisteredInPhysicsScene() const noexcept;

        /**
         * @brief Get the shape index in PhysicsScene.
         *
         * @return Physics shape index, or INVALID_INDEX if not registered.
         */
        uint32_t GetPhysicsShapeIndex() const noexcept;

        /**
         * @brief Get local box center in the parent object space.
         *
         * @return Local center position of this shape.
         */
        glm::vec3 GetLocalCenterInParentSpace() const;

        /**
         * @brief Get local box rotation in the parent object space.
         *
         * @return Local shape rotation quaternion.
         */
        glm::quat GetLocalRotationInParentSpace() const;

    public:
        REFL_SER_ENABLE CollisionShapeType m_shape_type{CollisionShapeType::Box};
        REFL_SER_ENABLE glm::vec3 m_box_size{1.0f, 1.0f, 1.0f};
        REFL_SER_ENABLE glm::vec3 m_box_center{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::quat m_box_rotation{1.0f, 0.0f, 0.0f, 0.0f};

    private:
        uint32_t m_shape_index{PhysicsScene::INVALID_INDEX};

        bool TryAttachToAncestorRigidBody();
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_PHYSICS_COLLISIONSHAPECOMPONENT_INCLUDED
