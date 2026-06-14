#include "SceneBuilder.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetRef.h>
#include <Core/Math/Transform.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/component/physics/CollisionShapeComponent.h>
#include <Framework/component/physics/RigidBodyComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Physics/PhysicsScene.h>
#include <Render/RenderSystem.h>

using namespace Engine;

SceneBuilder::SceneBuilder(Scene &scene,
                           FileSystemDatabase &adb,
                           GameObject &root,
                           RenderSystem &render_system) :
    m_scene(scene),
    m_adb(adb),
    m_root(root),
    m_render_system(render_system) {
    // Load the builtin cube mesh once — all boxes share it.
    m_cube_mesh = adb.GetNewAssetRef(AssetPath{adb, "~/mesh/cube.asset"});
}

GameObject &SceneBuilder::AddBox(const BoxDesc &desc) {
    // ┌─────────────────────────────────────────┐
    // │  Parent GO (RigidBodyComponent)          │
    // │  position = desc.position                │
    // │  parented to m_root                      │
    // ├─────────────────────────────────────────┤
    // │  ├─ "Mesh" child                         │
    // │  │   StaticMeshComponent (cube + material)│
    // │  │   scale = half_extents                  │
    // │  └─ "Collision" child                    │
    // │      CollisionShapeComponent (box)        │
    // │      m_box_size = half_extents            │
    // └─────────────────────────────────────────┘

    // --- Parent: holds RigidBody, positioned in world space ---
    GameObject &parent = m_scene.CreateGameObject();
    parent.SetParent(m_root.GetHandle());
    {
        Transform t;
        t.SetPosition(desc.position);
        parent.SetTransform(t);
    }

    auto &rb = parent.AddComponent<RigidBodyComponent>();
    rb.m_mass = desc.mass;
    rb.m_is_kinematic = desc.kinematic;

    // --- Mesh child: visual cube ---
    GameObject &mesh_child = m_scene.CreateGameObject();
    mesh_child.SetParent(parent.GetHandle());
    // Identity transform relative to parent (same position).
    {
        Transform t;
        mesh_child.SetTransform(t);
    }
    // Scale the cube mesh to match collision box dimensions.
    // The builtin cube is 2×2×2 centered at origin, so scale = half_extents directly.
    mesh_child.GetTransformRef().SetScale(desc.half_extents);

    auto &mc = mesh_child.AddComponent<StaticMeshComponent>();
    mc.m_mesh_asset = m_cube_mesh;
    mc.m_material_assets.push_back(desc.material);
    mc.m_is_eagerly_loaded = true;

    m_mesh_components.push_back(&mc);

    // --- Collision child: physics shape ---
    GameObject &collision_child = m_scene.CreateGameObject();
    collision_child.SetParent(parent.GetHandle());
    // Identity transform relative to parent (same position).
    {
        Transform t;
        collision_child.SetTransform(t);
    }

    auto &shape = collision_child.AddComponent<CollisionShapeComponent>();
    shape.m_shape_type = CollisionShapeType::Box;
    shape.m_box_size = desc.half_extents;

    return parent;
}

std::vector<StaticMeshComponent *> &SceneBuilder::GetMeshComponents() {
    return m_mesh_components;
}

void SceneBuilder::Finalize(PhysicsScene &physics_scene) {
    physics_scene.InitializePendingRigidBodies(m_render_system);
}
