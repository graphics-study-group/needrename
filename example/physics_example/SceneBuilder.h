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
    glm::vec3 half_extents{0.5f, 0.5f, 0.5f};
    float mass{1.0f};
    bool kinematic{false};
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
 *   builder.AddBox({.position = {2,0,5}, .half_extents = {1,1,1}, .kinematic = true, .material = grey_mat});
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
    SceneBuilder(Engine::Scene &scene,
                 Engine::FileSystemDatabase &adb,
                 Engine::GameObject &root,
                 Engine::RenderSystem &render_system);

    /**
     * @brief Add a physics box to the scene.
     *
     * Creates a parent GameObject (with RigidBodyComponent) and two children:
     * - "Mesh" child with StaticMeshComponent using the builtin cube mesh and
     *   the specified material, scaled by half_extents*2.
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
     * @brief Get the list of StaticMeshComponents created by AddBox.
     *
     * Callers should iterate this list and call PreRenderUpdate() on each
     * element after the scene has been initialized.
     */
    std::vector<Engine::StaticMeshComponent *> &GetMeshComponents();

    /**
     * @brief Finalize physics scene initialization.
     *
     * Calls physics_scene.InitializePendingRigidBodies(render_system).
     *
     * @param physics_scene The PhysicsScene to finalize.
     */
    void Finalize(Engine::PhysicsScene &physics_scene);

private:
    Engine::Scene &m_scene;
    Engine::FileSystemDatabase &m_adb;
    Engine::GameObject &m_root;
    Engine::RenderSystem &m_render_system;
    Engine::AssetRef m_cube_mesh;
    std::vector<Engine::StaticMeshComponent *> m_mesh_components;
};

#endif // EXAMPLE_PHYSICS_EXAMPLE_SCENEBUILDER_H
