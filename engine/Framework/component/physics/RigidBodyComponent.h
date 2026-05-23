#ifndef FRAMEWORK_COMPONENT_PHYSICS_RIGIDBODYCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_PHYSICS_RIGIDBODYCOMPONENT_INCLUDED

#include <Framework/component/Component.h>
#include <Physics/PhysicsScene.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>

#include <vector>

namespace Engine {
    class CollisionShapeComponent;
    class GameObject;

    /**
     * @brief Physics rigid body component.
     *
     * This component owns rigid body material and motion properties, and
     * aggregates collision shapes from an object hierarchy in Awake.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) RigidBodyComponent : public Component {
        REFL_SER_BODY(RigidBodyComponent)
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
         * The destructor attempts to unregister this rigid body from
         * PhysicsScene.
         */
        virtual ~RigidBodyComponent();

        /**
         * @brief Register or update the rigid body in PhysicsScene.
         *
         * Awake collects valid collision shapes from this object tree,
         * computes center-of-mass offsets, and updates rigid body mappings.
         */
        void Awake() override;

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

    private:
        /**
         * @brief Temporary per-shape record used during hierarchy collection.
         *
         * This carries intermediate geometry and transform data for a shape
         * before final mapping to the rigid body center frame.
         */
        struct ShapeCollectRecord {
            CollisionShapeComponent *shape_component{nullptr};
            GameObject *owner_object{nullptr};
            glm::vec3 local_center_in_root{0.0f, 0.0f, 0.0f};
            glm::quat local_rotation_in_root{1.0f, 0.0f, 0.0f, 0.0f};
            float volume{0.0f};
            uint32_t shape_index{PhysicsScene::INVALID_INDEX};
        };

        uint32_t m_rigid_body_index{PhysicsScene::INVALID_INDEX};

        void CollectShapesRecursively(
            GameObject *node,
            GameObject *root,
            std::vector<ShapeCollectRecord> &records,
            bool skip_rigidbody_check_on_node
        );

        RigidBodyPhysicsProperties BuildPhysicsProperties() const;
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_PHYSICS_RIGIDBODYCOMPONENT_INCLUDED
