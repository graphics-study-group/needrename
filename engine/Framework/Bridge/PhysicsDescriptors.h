#ifndef FRAMEWORK_WORLD_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED
#define FRAMEWORK_WORLD_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED

#include "Framework/framework_export.h"
#include <Framework/World/Handle.h>
#include <Physics/PhysicsScene.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

namespace Engine {

    /**
     * @brief Framework-internal GO-space transport structs.
     *
     * These descriptors carry data from Framework components into PhysicsAdaptor and are
     * NOT part of the physics-interface (PhysicsScene) input contract. All spatial values
     * are in GO space; the Adaptor converts them to COM space during Flush using the
     * COM-space descriptors declared in <Physics/PhysicsDescriptors.h>.
     */

    /**
     * @brief GO-space rigid body descriptor submitted from component to PhysicsAdaptor during Init.
     *
     * Carries component field values plus the owning GameObject's world transform.
     * All spatial values are in GO space; the Adaptor converts them to COM space during Flush.
     */
    struct FRAMEWORK_API RigidBodyDescriptor {
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

    /**
     * @brief GO-space collision shape descriptor submitted from component to PhysicsAdaptor during Init.
     *
     * Carries shape type, feature, GO-world pose, and unresolved collision filter ComponentHandles.
     * The Adaptor resolves filters and converts to COM-local pose during Flush.
     */
    struct FRAMEWORK_API CollisionShapeDescriptor {
        CollisionShapeType type{CollisionShapeType::Box};
        glm::vec3 feature{0.5f, 0.5f, 0.5f};
        glm::vec3 world_position{0.0f, 0.0f, 0.0f};
        glm::quat world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        std::vector<ComponentHandle> ignore_collision_shapes{};
    };

    /**
     * @brief GO-space submit data for a fixed joint connecting obj1 to obj2.
     *
     * obj1_index and obj2_index are rigid body slot indices.
     * initial_rel_pos_local and initial_rel_rotation are expressed in obj1's GO-local frame.
     */
    struct FRAMEWORK_API FixedJointSubmitData {
        uint32_t obj1_index{0};
        uint32_t obj2_index{0};
        float compliance{0.0f};
        glm::vec3 initial_rel_pos_local{0.0f, 0.0f, 0.0f};
        glm::quat initial_rel_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    /**
     * @brief GO-space submit data for a hinge joint connecting obj1 to obj2.
     *
     * Hinge axis, anchor point, and initial relative transform are expressed in obj1's GO-local frame.
     * The Adaptor converts them to COM-local during Flush.
     */
    struct FRAMEWORK_API HingeJointSubmitData {
        uint32_t obj1_index{0};
        uint32_t obj2_index{0};
        float compliance{0.0f};
        glm::vec3 hinge_axis_obj1{0.0f, 0.0f, 0.0f};
        glm::vec3 hinge_anchor_obj1{0.0f, 0.0f, 0.0f};
        glm::vec3 initial_rel_pos_local{0.0f, 0.0f, 0.0f};
        glm::quat initial_rel_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

} // namespace Engine

#endif // FRAMEWORK_WORLD_PHYSICS_PHYSICSDESCRIPTORS_INCLUDED
