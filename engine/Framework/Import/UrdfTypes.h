#ifndef FRAMEWORK_IMPORT_URDFTYPES_INCLUDED
#define FRAMEWORK_IMPORT_URDFTYPES_INCLUDED

#include "Framework/framework_export.h"
#include <Framework/World/Handle.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

    /// URDF geometry primitive types.
    enum class UrdfGeometryType {
        Box,
        Sphere,
        Cylinder,
        Mesh
    };

    /// Raw geometry data extracted from a URDF <geometry> element.
    struct FRAMEWORK_API UrdfGeometry {
        UrdfGeometryType type{UrdfGeometryType::Box};
        glm::vec3 box_size{0.0f};    // Full size (x, y, z) for Box
        float sphere_radius{0.0f};   // Radius for Sphere
        float cylinder_radius{0.0f}; // Radius for Cylinder
        float cylinder_length{0.0f}; // Full length for Cylinder
        std::string mesh_filename{}; // package:// path for Mesh (Phase 2)
        glm::vec3 mesh_scale{1.0f, 1.0f, 1.0f};
    };

    /// A positioned element (visual, collision) with origin + geometry.
    struct FRAMEWORK_API UrdfPosedElement {
        glm::vec3 origin_xyz{0.0f}; // URDF coordinates
        glm::vec3 origin_rpy{0.0f}; // Euler angles in radians (URDF frame)
        UrdfGeometry geometry{};
    };

    /// Inertial parameters for a link.
    struct FRAMEWORK_API UrdfInertial {
        glm::vec3 origin_xyz{0.0f}; // COM offset in link frame (URDF coords)
        glm::vec3 origin_rpy{0.0f}; // COM orientation (URDF frame)
        float mass{0.0f};
        // Symmetric 3x3 inertia tensor components: ixx, ixy, ixz, iyy, iyz, izz
        float ixx{0.0f}, ixy{0.0f}, ixz{0.0f}, iyy{0.0f}, iyz{0.0f}, izz{0.0f};
    };

    /// URDF joint types.
    enum class UrdfJointType {
        Fixed,
        Revolute,
        Continuous,
        Prismatic,
        Floating,
        Unknown
    };

    /// A URDF <joint> element.
    struct FRAMEWORK_API UrdfJoint {
        std::string name{};
        UrdfJointType type{UrdfJointType::Fixed};
        std::string parent_link{};
        std::string child_link{};
        glm::vec3 origin_xyz{0.0f};       // URDF coordinates
        glm::vec3 origin_rpy{0.0f};       // Euler angles in radians (URDF frame)
        glm::vec3 axis{1.0f, 0.0f, 0.0f}; // For revolute / continuous / prismatic
        float damping{0.0f};
        float friction{0.0f};
        float limit_lower{0.0f};
        float limit_upper{0.0f};
        float limit_effort{0.0f};
        float limit_velocity{0.0f};
    };

    /// A URDF <link> element.
    struct FRAMEWORK_API UrdfLink {
        std::string name{};
        std::vector<UrdfPosedElement> visuals{};
        std::vector<UrdfPosedElement> collisions{};
        std::optional<UrdfInertial> inertial{};
    };

    /// A URDF <material> definition.
    struct FRAMEWORK_API UrdfMaterial {
        std::string name{};
        glm::vec4 color_rgba{1.0f, 1.0f, 1.0f, 1.0f};
    };

    /// Complete parsed URDF robot.
    struct FRAMEWORK_API UrdfRobot {
        std::string name{};
        std::vector<UrdfLink> links{};
        std::vector<UrdfJoint> joints{};
        std::vector<UrdfMaterial> materials{};
        std::string root_link_name{};
    };

    /**
     * @brief Options controlling how a robot is built into a scene.
     *
     * `position`/`rotation` place the robot as a whole (applied to the root
     * link). `static_friction`/`dynamic_friction`/`restitution` are applied
     * uniformly to every created `RigidBodyComponent`. `with_visuals` toggles
     * the visual mesh assembly.
     */
    struct FRAMEWORK_API UrdfBuildOptions {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        float static_friction{0.5f};
        float dynamic_friction{0.5f};
        float restitution{0.0f};
        bool with_visuals{true};
    };

    /**
     * @brief A joint's two endpoint objects, in URDF parent/child order.
     *
     * `parent` is the link closer to the robot root. Both handles belong to
     * links that received a `RigidBodyComponent`.
     */
    struct FRAMEWORK_API UrdfBuiltJoint {
        ObjectHandle parent{};
        ObjectHandle child{};

        /**
         * @brief Hinge axis of the joint in the parent link's GO frame,
         * engine coordinates. Same value as the `HingeJointDef.m_hinge_axis_obj1`
         * conversion (`UrdfAxisToEngine(joint.axis)`). Meaningful only for joints
         * that produced a physical `HingeJointDef`; left at the `UrdfJoint.axis`
         * default otherwise.
         */
        glm::vec3 axis{1.0f, 0.0f, 0.0f};
    };

    /**
     * @brief Result of building a robot into a scene.
     *
     * `link_objects` maps link name to its GameObject handle for links that
     * received a `RigidBodyComponent` (links with `<inertial>`). `joint_objects`
     * maps joint name to its endpoint handles for joints that produced a
     * physical constraint. Both maps omit entities that were not physically
     * realized.
     */
    struct FRAMEWORK_API UrdfBuiltRobot {
        std::unordered_map<std::string, ObjectHandle> link_objects{};
        std::unordered_map<std::string, UrdfBuiltJoint> joint_objects{};
    };

} // namespace Engine

#endif // FRAMEWORK_IMPORT_URDFTYPES_INCLUDED
