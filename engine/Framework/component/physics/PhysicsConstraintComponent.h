#ifndef FRAMEWORK_COMPONENT_PHYSICS_PHYSICSCONSTRAINTCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_PHYSICS_PHYSICSCONSTRAINTCOMPONENT_INCLUDED

#include <Framework/component/Component.h>
#include <Framework/world/Handle.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <variant>
#include <vector>

namespace Engine {
    /**
     * @brief Fixed joint definition stored on PhysicsConstraintComponent.
     *
     * obj1 is implicitly the owner of the component. The initial relative
     * transform is computed at Awake() time from current world transforms.
     */
    struct FixedJointDef {
        ObjectHandle m_obj2_handle{}; ///< Handle of the second object.
        float m_compliance{0.0f};     ///< Joint compliance (0 = hard constraint).
    };

    /**
     * @brief Hinge joint definition stored on PhysicsConstraintComponent.
     *
     * obj1 is implicitly the owner of the component. Obj2-local values are
     * derived automatically at Awake() time from the initial relative transform.
     * No angle limits or target angle support in this version.
     */
    struct HingeJointDef {
        ObjectHandle m_obj2_handle;    ///< Handle of the second object.
        glm::vec3 m_hinge_axis_obj1;   ///< Hinge axis in obj1's local frame (will be normalized).
        glm::vec3 m_hinge_anchor_obj1; ///< Hinge anchor point in obj1's local frame.
        float m_compliance{0.0f};      ///< Joint compliance (0 = hard constraint).
    };

    /// Variant type for storing either joint type.
    using JointVariant = std::variant<FixedJointDef, HingeJointDef>;

    /**
     * @brief Physics constraint component for GPU XPBD joint constraints.
     *
     * Stores arbitrary numbers of FixedJoint and HingeJoint definitions.
     * Lives on obj1 (the "owning" body). During Awake(), validates that both
     * obj1 and obj2 have RigidBodyComponent, resolves handles to rigid body
     * indices, computes implicit FixedJoint initial relative transforms, and
     * registers joints with PhysicsScene.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) PhysicsConstraintComponent : public Component {
        REFL_SER_BODY(PhysicsConstraintComponent)
    public:
        /**
         * @brief Construct a constraint component.
         *
         * @param parent Parent game object (also obj1 for all joints).
         */
        REFL_ENABLE explicit PhysicsConstraintComponent(const GameObject &parent);

        /**
         * @brief Destroy the constraint component.
         */
        virtual ~PhysicsConstraintComponent();

        /**
         * @brief Validate and register all joints with PhysicsScene.
         *
         * Validates that both obj1 and each obj2 have RigidBodyComponent.
         * Computes initial relative transform for FixedJoints.
         * Logs errors and skips invalid joints without crashing.
         */
        void Awake() override;

        /**
         * @brief Resolve handles and upload joint data to PhysicsScene.
         *
         * Init resolves ObjectHandles to rigid body indices (guaranteed
         * available since all RigidBody Awake callbacks have completed),
         * recomputes FixedJoint initial relative transforms from current
         * world poses, and fills the allocated joint slots.
         */
        void Init() override;

        /**
         * @brief Custom serialization for m_joints variant vector.
         *
         * The reflection system cannot handle std::variant, so we manually
         * write each joint as a tagged JSON object.
         */
        void save_to_archive(Serialization::Archive &archive) const;

        /**
         * @brief Custom deserialization for m_joints variant vector.
         */
        void load_from_archive(Serialization::Archive &archive);

    public:
        /// Joint definitions.
        std::vector<JointVariant> m_joints{};

    private:
        /// Cached joint indices from Awake allocation, parallel to m_joints.
        std::vector<std::pair<uint32_t, bool>> m_joint_indices{};
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_PHYSICS_PHYSICSCONSTRAINTCOMPONENT_INCLUDED
