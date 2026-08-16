#ifndef ENGINE_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED
#define ENGINE_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED

#include "physics_export.h"

#include <glm.hpp>

#include <cstdint>

namespace Engine {

    /**
     * @brief COM-space rigid body descriptor submitted from PhysicsAdaptor to PhysicsScene during Flush.
     *
     * All spatial fields are in COM space. The Adaptor has already computed
     * center-of-mass position, rotation, inertia, and inverse inertia.
     * This descriptor writes directly into PhysicsScene SoA columns.
     */
    struct PHYSICS_API RigidBodyComDescriptor {
        float mass{1.0f};
        float static_friction{0.5f};
        float dynamic_friction{0.5f};
        float restitution{0.0f};
        bool is_kinematic{false};
        glm::vec4 center_world_position{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 center_world_rotation{0.0f, 0.0f, 0.0f, 0.0f};
        glm::mat4 inertia{0.0f};
        glm::mat4 inverse_inertia{0.0f};
        glm::vec4 linear_velocity{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 angular_velocity{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 external_force{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 external_torque{0.0f, 0.0f, 0.0f, 0.0f};
    };

    /**
     * @brief COM-space collision shape descriptor submitted from PhysicsAdaptor to PhysicsScene during Flush.
     *
     * local_position and local_rotation are COM-local values computed by the Adaptor.
     * bound_rigid_body links the shape to its owning rigid body (INVALID_INDEX if unbound).
     * The GPU computes the world pose from the COM pose plus the shape local pose.
     */
    struct PHYSICS_API CollisionShapeComDescriptor {
        uint32_t type{0};
        glm::vec4 feature{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 local_position{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 local_rotation{0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t bound_rigid_body{0xFFFFFFFFu};
    };

    /**
     * @brief COM-space fixed joint definition uploaded to the GPU (std430, 48 bytes).
     *
     * obj1_index and obj2_index are rigid body slot indices.
     * initial_rel_pos_local and initial_rel_rotation are expressed in obj1's COM-local frame.
     * Layout must stay in sync with the GLSL struct in accumulate_fixed_position.comp.
     */
    struct PHYSICS_API FixedJointComDescriptor {
        uint32_t obj1_index;
        uint32_t obj2_index;
        float compliance;
        float _pad;
        glm::vec4 initial_rel_pos_local;
        glm::vec4 initial_rel_rotation;
    };

    /**
     * @brief COM-space hinge joint definition uploaded to the GPU (std430, 80 bytes).
     *
     * Hinge axis, anchor point, and initial relative transform are expressed in obj1's COM-local frame.
     * Layout must stay in sync with the GLSL struct in accumulate_hinge_position.comp.
     */
    struct PHYSICS_API HingeJointComDescriptor {
        uint32_t obj1_index;
        uint32_t obj2_index;
        float compliance;
        float _pad;
        glm::vec4 hinge_axis_obj1;
        glm::vec4 hinge_anchor_obj1;
        glm::vec4 initial_rel_pos_local;
        glm::vec4 initial_rel_rotation;
    };

    static_assert(sizeof(FixedJointComDescriptor) == 48, "FixedJointComDescriptor must be 48 bytes (std430)");
    static_assert(sizeof(HingeJointComDescriptor) == 80, "HingeJointComDescriptor must be 80 bytes (std430)");

} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED
