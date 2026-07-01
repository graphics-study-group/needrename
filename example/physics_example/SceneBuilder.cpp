#include "SceneBuilder.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetRef.h>
#include <Core/Math/Transform.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/component/physics/CollisionShapeComponent.h>
#include <Framework/component/physics/PhysicsConstraintComponent.h>
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

GameObject &SceneBuilder::AddDoublePendulum(const glm::vec3 &anchor_position) {
    constexpr float kSphereRadius = 0.1f;
    constexpr float kBoxHalfX = 0.05f;
    constexpr float kBoxHalfY = 0.05f;
    constexpr float kBoxHalfZ = 0.6f;
    constexpr float kCylRadius = 0.2f;
    constexpr float kCylHalfH = 0.5f;
    constexpr float kGap = 0.1f; // clearance between connected bodies to prevent collision

    const glm::vec3 kAlignedAxis(0.0f, 1.0f, 0.0f); // hinge rotates around Y (swing in XZ plane)

    // --- 1. Kinematic anchor sphere ---
    GameObject &sphere = AddSphere({
        .position = anchor_position,
        .radius = kSphereRadius,
        .mass = 0.0f,
        .kinematic = true,
        .material = m_adb.GetNewAssetRef(AssetPath{m_adb, "~/materials/solid_color_white.asset"}),
    });

    // --- 2. First pendulum box ---
    float box1_x = anchor_position.x - kSphereRadius - kGap - kBoxHalfZ;
    GameObject &box1 = AddBox({
        .position = glm::vec3(box1_x, anchor_position.y, anchor_position.z),
        .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        .half_extents = glm::vec3(kBoxHalfX, kBoxHalfY, kBoxHalfZ),
        .mass = 1.0f,
        .material = m_adb.GetNewAssetRef(AssetPath{m_adb, "~/materials/solid_color_red.asset"}),
    });

    // --- 3. Second pendulum box ---
    float box2_x = box1_x - kBoxHalfZ - kGap - kBoxHalfZ;
    GameObject &box2 = AddBox({
        .position = glm::vec3(box2_x, anchor_position.y, anchor_position.z),
        .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        .half_extents = glm::vec3(kBoxHalfX, kBoxHalfY, kBoxHalfZ),
        .mass = 1.0f,
        .material = m_adb.GetNewAssetRef(AssetPath{m_adb, "~/materials/solid_color_green.asset"}),
    });

    // --- 4. Bottom cylinder (oriented horizontally for visible rotation) ---
    float cyl_x = box2_x - kBoxHalfZ - kGap - kCylRadius;
    GameObject &cylinder = AddCylinder({
        .position = glm::vec3(cyl_x, anchor_position.y, anchor_position.z),
        .rotation = glm::quat(),
        .radius = kCylRadius,
        .half_height = kCylHalfH,
        .mass = 5.0f,
        .material = m_adb.GetNewAssetRef(AssetPath{m_adb, "~/materials/solid_color_blue.asset"}),
    });

    // --- 5. HingeJoint: sphere → box1 ---
    {
        auto &constraint = sphere.AddComponent<PhysicsConstraintComponent>();
        HingeJointDef hinge{};
        hinge.m_obj2_handle = box1.GetHandle();
        hinge.m_compliance = 0.0001f;
        hinge.m_obj1_local_aligned_axis = kAlignedAxis;
        hinge.m_obj2_local_aligned_axis = kAlignedAxis;
        // attach at sphere bottom → box1 top
        hinge.m_obj1_local_attach_point = glm::vec3(0.0f, 0.0f, 0.0f);
        hinge.m_obj2_local_attach_point = glm::vec3(0.0f, 0.0f, kBoxHalfZ + kGap + kSphereRadius);
        constraint.m_joints.push_back(hinge);
    }

    // --- 6. HingeJoint: box1 → box2 ---
    {
        auto &constraint = box1.AddComponent<PhysicsConstraintComponent>();
        HingeJointDef hinge{};
        hinge.m_obj2_handle = box2.GetHandle();
        hinge.m_compliance = 0.0001f;
        hinge.m_obj1_local_aligned_axis = kAlignedAxis;
        hinge.m_obj2_local_aligned_axis = kAlignedAxis;
        // attach at box1 bottom → box2 top
        hinge.m_obj1_local_attach_point = glm::vec3(0.0f, 0.0f, -kBoxHalfZ - 0.5f * kGap);
        hinge.m_obj2_local_attach_point = glm::vec3(0.0f, 0.0f, kBoxHalfZ + 0.5f * kGap);
        constraint.m_joints.push_back(hinge);
    }

    // --- 7. FixedJoint: box2 → cylinder ---
    {
        auto &constraint = box2.AddComponent<PhysicsConstraintComponent>();
        FixedJointDef fixed{};
        fixed.m_obj2_handle = cylinder.GetHandle();
        fixed.m_compliance = 0.0f;
        constraint.m_joints.push_back(fixed);
    }

    return sphere;
}

void SceneBuilder::Finalize(PhysicsScene &physics_scene) {
    physics_scene.InitializePendingRigidBodies(m_render_system);
}
