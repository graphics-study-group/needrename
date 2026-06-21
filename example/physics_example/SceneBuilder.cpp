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

SceneBuilder::SceneBuilder(Scene &scene, FileSystemDatabase &adb, GameObject &root, RenderSystem &render_system) :
    m_scene(scene), m_adb(adb), m_root(root), m_render_system(render_system) {
    // Load the builtin meshes once — all instances share them.
    m_cube_mesh = adb.GetNewAssetRef(AssetPath{adb, "~/mesh/cube.asset"});
    m_sphere_mesh = adb.GetNewAssetRef(AssetPath{adb, "~/mesh/sphere.asset"});
    m_cylinder_mesh = adb.GetNewAssetRef(AssetPath{adb, "~/mesh/cylinder.asset"});
}

GameObject &SceneBuilder::AddRigidBodyObject(
    const glm::vec3 &position,
    const glm::quat &rotation,
    float mass,
    bool kinematic,
    float static_friction,
    float dynamic_friction,
    float restitution
) {
    GameObject &parent = m_scene.CreateGameObject();
    parent.SetParent(m_root.GetHandle());
    {
        Transform t;
        t.SetPosition(position);
        t.SetRotation(rotation);
        parent.SetTransform(t);
    }

    auto &rb = parent.AddComponent<RigidBodyComponent>();
    rb.m_mass = mass;
    rb.m_is_kinematic = kinematic;
    rb.m_static_friction = static_friction;
    rb.m_dynamic_friction = dynamic_friction;
    rb.m_restitution = restitution;

    return parent;
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
    // │      m_feature = half_extents            │
    // └─────────────────────────────────────────┘

    // --- Parent: holds RigidBody, positioned in world space ---
    GameObject &parent = AddRigidBodyObject(
        desc.position,
        desc.rotation,
        desc.mass,
        desc.kinematic,
        desc.static_friction,
        desc.dynamic_friction,
        desc.restitution
    );

    // --- Mesh child: visual cube ---
    GameObject &mesh_child = m_scene.CreateGameObject();
    mesh_child.SetParent(parent.GetHandle());
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
    {
        Transform t;
        collision_child.SetTransform(t);
    }

    auto &shape = collision_child.AddComponent<CollisionShapeComponent>();
    shape.m_shape_type = CollisionShapeType::Box;
    shape.m_feature = desc.half_extents;

    return parent;
}

GameObject &SceneBuilder::AddSphere(const SphereDesc &desc) {
    // ┌─────────────────────────────────────────┐
    // │  Parent GO (RigidBodyComponent)          │
    // │  position = desc.position                │
    // │  parented to m_root                      │
    // ├─────────────────────────────────────────┤
    // │  ├─ "Mesh" child                         │
    // │  │   StaticMeshComponent (sphere + mat)   │
    // │  │   scale = vec3(radius)                │
    // │  └─ "Collision" child                    │
    // │      CollisionShapeComponent (sphere)     │
    // │      m_feature = {radius, 0, 0}          │
    // └─────────────────────────────────────────┘

    // --- Parent ---
    GameObject &parent = AddRigidBodyObject(
        desc.position,
        desc.rotation,
        desc.mass,
        desc.kinematic,
        desc.static_friction,
        desc.dynamic_friction,
        desc.restitution
    );

    // --- Mesh child: visual sphere ---
    GameObject &mesh_child = m_scene.CreateGameObject();
    mesh_child.SetParent(parent.GetHandle());
    {
        Transform t;
        mesh_child.SetTransform(t);
    }
    // Builtin sphere has radius 1m, so scale = vec3(radius) directly.
    const glm::vec3 sphere_scale(desc.radius);
    mesh_child.GetTransformRef().SetScale(sphere_scale);

    auto &mc = mesh_child.AddComponent<StaticMeshComponent>();
    mc.m_mesh_asset = m_sphere_mesh;
    mc.m_material_assets.push_back(desc.material);
    mc.m_is_eagerly_loaded = true;

    m_mesh_components.push_back(&mc);

    // --- Collision child ---
    GameObject &collision_child = m_scene.CreateGameObject();
    collision_child.SetParent(parent.GetHandle());
    {
        Transform t;
        collision_child.SetTransform(t);
    }

    auto &shape = collision_child.AddComponent<CollisionShapeComponent>();
    shape.m_shape_type = CollisionShapeType::Sphere;
    shape.m_feature = glm::vec3(desc.radius, 0.0f, 0.0f);

    return parent;
}

GameObject &SceneBuilder::AddCylinder(const CylinderDesc &desc) {
    // ┌─────────────────────────────────────────┐
    // │  Parent GO (RigidBodyComponent)          │
    // │  position = desc.position                │
    // │  parented to m_root                      │
    // ├─────────────────────────────────────────┤
    // │  ├─ "Mesh" child                         │
    // │  │   StaticMeshComponent (cylinder + mat) │
    // │  │   scale = vec3(radius, radius, half_h)│
    // │  └─ "Collision" child                    │
    // │      CollisionShapeComponent (cylinder)   │
    // │      m_feature = {radius, half_h, 0}     │
    // └─────────────────────────────────────────┘

    // --- Parent ---
    GameObject &parent = AddRigidBodyObject(
        desc.position,
        desc.rotation,
        desc.mass,
        desc.kinematic,
        desc.static_friction,
        desc.dynamic_friction,
        desc.restitution
    );

    // --- Mesh child: visual cylinder (Z-up) ---
    GameObject &mesh_child = m_scene.CreateGameObject();
    mesh_child.SetParent(parent.GetHandle());
    {
        Transform t;
        mesh_child.SetTransform(t);
    }
    // Builtin cylinder has radius 1m, height 2m (Z-up), centered at origin.
    // scale = (radius, radius, half_height) maps directly.
    const glm::vec3 cylinder_scale(desc.radius, desc.radius, desc.half_height);
    mesh_child.GetTransformRef().SetScale(cylinder_scale);

    auto &mc = mesh_child.AddComponent<StaticMeshComponent>();
    mc.m_mesh_asset = m_cylinder_mesh;
    mc.m_material_assets.push_back(desc.material);
    mc.m_is_eagerly_loaded = true;

    m_mesh_components.push_back(&mc);

    // --- Collision child ---
    GameObject &collision_child = m_scene.CreateGameObject();
    collision_child.SetParent(parent.GetHandle());
    {
        Transform t;
        collision_child.SetTransform(t);
    }

    auto &shape = collision_child.AddComponent<CollisionShapeComponent>();
    shape.m_shape_type = CollisionShapeType::Cylinder;
    shape.m_feature = glm::vec3(desc.radius, desc.half_height, 0.0f);

    return parent;
}

std::vector<StaticMeshComponent *> &SceneBuilder::GetMeshComponents() {
    return m_mesh_components;
}

void SceneBuilder::Finalize(PhysicsScene &physics_scene) {
    physics_scene.InitializePendingRigidBodies(m_render_system);
}
