#ifndef FRAMEWORK_WORLD_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED
#define FRAMEWORK_WORLD_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED

#include <Framework/world/Handle.h>
#include <Physics/PhysicsScene.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cstdint>
#include <variant>
#include <vector>

namespace Engine {

    // -------------------------------------------------------------------------
    // GO-space Descriptors — component → Adaptor (Init phase)
    // -------------------------------------------------------------------------

    struct RigidBodyDescriptor {
        float mass{1.0f};
        float static_friction{0.5f};
        float dynamic_friction{0.5f};
        float restitution{0.0f};
        bool is_kinematic{false};
        glm::vec3 world_position{0.0f, 0.0f, 0.0f};
        glm::quat world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 linear_velocity{0.0f, 0.0f, 0.0f};
        glm::vec3 angular_velocity{0.0f, 0.0f, 0.0f};
        glm::vec3 external_force{0.0f, 0.0f, 0.0f};
        glm::vec3 external_torque{0.0f, 0.0f, 0.0f};
        bool use_manual_inertia_com{false};
        glm::mat3 manual_inertia{0.0f};
        glm::vec3 manual_center_of_mass{0.0f, 0.0f, 0.0f};
    };

    struct CollisionShapeDescriptor {
        CollisionShapeType type{CollisionShapeType::Box};
        glm::vec3 feature{0.5f, 0.5f, 0.5f};
        glm::vec3 world_position{0.0f, 0.0f, 0.0f};
        glm::quat world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        std::vector<ObjectHandle> ignore_collision_objects{};
    };

    struct FixedJointSubmitData {
        uint32_t obj1_index{0};
        uint32_t obj2_index{0};
        float compliance{0.0f};
        glm::vec3 initial_rel_pos_local{0.0f, 0.0f, 0.0f};
        glm::quat initial_rel_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    struct HingeJointSubmitData {
        uint32_t obj1_index{0};
        uint32_t obj2_index{0};
        float compliance{0.0f};
        glm::vec3 hinge_axis_obj1{0.0f, 0.0f, 0.0f};
        glm::vec3 hinge_anchor_obj1{0.0f, 0.0f, 0.0f};
        glm::vec3 initial_rel_pos_local{0.0f, 0.0f, 0.0f};
        glm::quat initial_rel_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    using JointSubmitData = std::variant<FixedJointSubmitData, HingeJointSubmitData>;

    // -------------------------------------------------------------------------
    // COM-space Descriptors — Adaptor → PhysicsScene (Flush phase)
    // -------------------------------------------------------------------------

    struct RigidBodyComDescriptor {
        float mass{1.0f};
        float static_friction{0.5f};
        float dynamic_friction{0.5f};
        float restitution{0.0f};
        bool is_kinematic{false};
        glm::vec4 center_world_position{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 center_world_rotation{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 center_offset_local_position{0.0f, 0.0f, 0.0f, 0.0f};
        glm::mat4 inertia{0.0f};
        glm::mat4 inverse_inertia{0.0f};
        glm::vec4 linear_velocity{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 angular_velocity{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 external_force{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 external_torque{0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct CollisionShapeComDescriptor {
        uint32_t type{0};
        glm::vec4 feature{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 local_position{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 local_rotation{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 world_position{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 world_rotation{0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t bound_rigid_body{PhysicsScene::INVALID_INDEX};
        std::vector<uint32_t> ignore_shape_indices{};
    };

} // namespace Engine

#endif // FRAMEWORK_WORLD_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED
