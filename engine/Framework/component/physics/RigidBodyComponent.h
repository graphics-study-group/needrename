#ifndef FRAMEWORK_COMPONENT_PHYSICS_RIGIDBODYCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_PHYSICS_RIGIDBODYCOMPONENT_INCLUDED

#include <Framework/component/Component.h>
#include <Framework/world/physics/PhysicsDescriptors.h>
#include <AnnoRefl/macros.h>
#include <AnnoRefl/serialization_glm.h>

namespace Engine {
    class CollisionShapeComponent;
    class GameObject;
    class PhysicsAdaptor;

    /**
     * @brief Physics rigid body component.
     *
     * This component owns rigid body material and motion properties, and
     * aggregates collision shapes from an object hierarchy via PhysicsAdaptor.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) RigidBodyComponent : public Component {
        REFL_SER_BODY_OVERRIDE(RigidBodyComponent)
    public:
        /**
         * @brief Construct a rigid body component.
         *
         * @param parent Parent game object.
         */
        REFL_ENABLE explicit RigidBodyComponent(const GameObject &parent);

        /**
         * @brief Destroy the rigid body component.
         *
         * The destructor attempts to unregister this rigid body through
         * PhysicsAdaptor.
         */
        virtual ~RigidBodyComponent();

        /**
         * @brief Allocate a rigid body slot via PhysicsAdaptor.
         */
        void Awake() override;

        /**
         * @brief Build a RigidBodyDescriptor and submit to PhysicsAdaptor,
         *        then collect and bind collision shapes.
         */
        void Init() override;

        /**
         * @brief Get the rigid body index in PhysicsScene.
         *
         * @return Physics rigid body index, or INVALID_INDEX if unregistered.
         */
        uint32_t GetPhysicsRigidBodyIndex() const noexcept;

    public:
        REFL_SER_ENABLE float m_mass{1.0f};
        REFL_SER_ENABLE float m_static_friction{0.5f};
        REFL_SER_ENABLE float m_dynamic_friction{0.5f};
        REFL_SER_ENABLE float m_restitution{0.0f};
        REFL_SER_ENABLE bool m_is_kinematic{false};
        REFL_SER_ENABLE glm::vec3 m_linear_velocity{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_angular_velocity_axis_angle{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_external_force{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_external_torque{0.0f, 0.0f, 0.0f};

        // Manual inertia/COM override â€?when enabled, PhysicsScene skips
        // automatic volume-weighted computation and uses these values directly.
        // Both inertia tensor and center-of-mass MUST be provided.
        // COM offset is in GO-local space.
        // Diagonal: (ixx, iyy, izz).  Off-diagonal: (ixy, ixz, iyz).
        REFL_SER_ENABLE bool m_use_manual_inertia_com{false};
        REFL_SER_ENABLE glm::vec3 m_manual_inertia_diag{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_manual_inertia_offdiag{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_manual_center_of_mass{0.0f, 0.0f, 0.0f};

    private:
        uint32_t m_rigid_body_index{PhysicsScene::INVALID_INDEX};

        void CollectShapesRecursivelyAndBind(
            GameObject *node, PhysicsAdaptor &adaptor, bool skip_rigidbody_check_on_node
        );
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_PHYSICS_RIGIDBODYCOMPONENT_INCLUDED
