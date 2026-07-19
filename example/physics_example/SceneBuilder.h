#ifndef EXAMPLE_PHYSICS_EXAMPLE_SCENEBUILDER_H
#define EXAMPLE_PHYSICS_EXAMPLE_SCENEBUILDER_H

#include <Asset/AssetRef.h>
#include <Framework/component/Component.h>
#include <glm.hpp>
#include <vector>

namespace Engine {
    class Scene;
    class GameObject;
    class StaticMeshComponent;
    class PhysicsScene;
    class RenderSystem;
    class FileSystemDatabase;
} // namespace Engine

/**
 * @brief Descriptor for creating a physics box object.
 *
 * All fields have sensible defaults so callers can use C++20 designated
 * initializers to set only what they need.
 */
struct BoxDesc {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 half_extents{0.5f, 0.5f, 0.5f};
    float mass{1.0f};
    bool kinematic{false};
    float static_friction{0.5f};
    float dynamic_friction{0.5f};
    float restitution{0.0f};
    Engine::AssetRef material{};
};

/**
 * @brief Descriptor for creating a physics sphere object.
 *
 * All fields have sensible defaults so callers can use C++20 designated
 * initializers to set only what they need.
 */
struct SphereDesc {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius{0.5f};
    float mass{1.0f};
    bool kinematic{false};
    float static_friction{0.5f};
    float dynamic_friction{0.5f};
    float restitution{0.0f};
    Engine::AssetRef material{};
};

/**
 * @brief Descriptor for creating a physics cylinder object (Z-up).
 *
 * All fields have sensible defaults so callers can use C++20 designated
 * initializers to set only what they need.
 */
struct CylinderDesc {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius{0.5f};
    float half_height{0.5f};
    float mass{1.0f};
    bool kinematic{false};
    float static_friction{0.5f};
    float dynamic_friction{0.5f};
    float restitution{0.0f};
    Engine::AssetRef material{};
};

/**
 * @brief Convenience helper for building GPU physics example scenes.
 *
 * SceneBuilder encapsulates the boilerplate of creating GameObjects, wiring
 * RigidBody / CollisionShape / StaticMesh components, assigning materials, and
 * tracking mesh components for PreRenderUpdate.
 *
 * Usage:
 * @code
 *   SceneBuilder builder(scene, adb, root, render_system);
 *   builder.AddBox({.position = {0,0,5}, .material = red_mat});
 *   builder.AddSphere({.position = {2,0,5}, .radius = 1.0f, .material = grey_mat});
 *   builder.AddCylinder({.position = {4,0,5}, .radius = 0.5f, .half_height = 1.0f, .material = blue_mat});
 *   auto &meshes = builder.GetMeshComponents();
 *   builder.Finalize(*physics_scene);
 * @endcode
 */
class SceneBuilder {
public:
    /**
     * @brief Construct a SceneBuilder.
     *
     * @param scene         Scene to create objects in.
     * @param adb           Asset database for resolving mesh/material paths.
     * @param root          Root GameObject all created objects will be parented to.
     * @param render_system Render system, used by Finalize for GPU buffer refresh.
     */
    SceneBuilder(
        Engine::Scene &scene,
        Engine::FileSystemDatabase &adb,
        Engine::GameObject &root
    );

    /**
     * @brief Add a physics box to the scene.
     *
     * Creates a parent GameObject (with RigidBodyComponent) and two children:
     * - "Mesh" child with StaticMeshComponent using the builtin cube mesh and
     *   the specified material, scaled by half_extents.
     * - "Collision" child with CollisionShapeComponent (box shape).
     *
     * The parent is positioned at desc.position and parented to the root.
     * Both children have identity local transforms; the mesh scale is
     * decoupled from the collision geometry.
     *
     * @param desc Box configuration. Unset fields use defaults.
     * @return Reference to the created parent GameObject.
     */
    Engine::GameObject &AddBox(const BoxDesc &desc);

    /**
     * @brief Add a physics sphere to the scene.
     *
     * Creates a parent GameObject (with RigidBodyComponent) and two children:
     * - "Mesh" child with StaticMeshComponent using the builtin sphere mesh
     *   (radius 1m) and the specified material, scaled by vec3(radius).
     * - "Collision" child with CollisionShapeComponent (sphere shape,
     *   feature = {radius, 0, 0}).
     *
     * @param desc Sphere configuration. Unset fields use defaults.
     * @return Reference to the created parent GameObject.
     */
    Engine::GameObject &AddSphere(const SphereDesc &desc);

    /**
     * @brief Add a physics cylinder (Z-up) to the scene.
     *
     * Creates a parent GameObject (with RigidBodyComponent) and two children:
     * - "Mesh" child with StaticMeshComponent using the builtin cylinder mesh
     *   (height 2m, radius 1m, Z-up) and the specified material, scaled by
     *   vec3(radius, radius, half_height).
     * - "Collision" child with CollisionShapeComponent (cylinder shape,
     *   feature = {radius, half_height, 0}).
     *
     * @param desc Cylinder configuration. Unset fields use defaults.
     * @return Reference to the created parent GameObject.
     */
    Engine::GameObject &AddCylinder(const CylinderDesc &desc);

    /**
     * @brief Get the list of StaticMeshComponents created by Add* methods.
     *
     * Callers should iterate this list and call PreRenderUpdate() on each
     * element after the scene has been initialized.
     */
    std::vector<Engine::StaticMeshComponent *> &GetMeshComponents();

    /**
     * @brief Add a double pendulum demonstration assembly.
     *
     * Creates a kinematic anchor sphere, two dynamic elongated boxes connected
     * by hinge joints, and a cylinder rigidly attached to the bottom box via a
     * FixedJoint. Spacing is added between bodies to prevent collision.
     *
     * @param anchor_position World position of the anchor sphere.
     * @return Reference to the anchor sphere GameObject.
     */
    Engine::GameObject &AddDoublePendulum(const glm::vec3 &anchor_position);

private:
    Engine::GameObject &AddRigidBodyObject(
        const glm::vec3 &position,
        const glm::quat &rotation,
        float mass,
        bool kinematic,
        float static_friction,
        float dynamic_friction,
        float restitution
    );

    Engine::Scene &m_scene;
    Engine::FileSystemDatabase &m_adb;
    Engine::GameObject &m_root;
    Engine::AssetRef m_cube_mesh;
    Engine::AssetRef m_sphere_mesh;
    Engine::AssetRef m_cylinder_mesh;
    std::vector<Engine::StaticMeshComponent *> m_mesh_components;
};

#endif // EXAMPLE_PHYSICS_EXAMPLE_SCENEBUILDER_H
