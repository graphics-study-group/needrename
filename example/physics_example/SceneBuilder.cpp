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
    // 1. Create the GameObject and parent it to root.
    GameObject &go = m_scene.CreateGameObject();
    go.SetParent(m_root.GetHandle());

    // 2. Set world-space position.
    {
        Transform t;
        t.SetPosition(desc.position);
        go.SetTransform(t);
    }

    // 3. Rigid body.
    auto &rb = go.AddComponent<RigidBodyComponent>();
    rb.m_mass = desc.mass;
    rb.m_is_kinematic = desc.kinematic;

    // 4. Collision shape (box).
    auto &shape = go.AddComponent<CollisionShapeComponent>();
    shape.m_shape_type = CollisionShapeType::Box;
    shape.m_box_size = desc.half_extents;

    // 5. Visual mesh (cube) with the specified material.
    auto &mc = go.AddComponent<StaticMeshComponent>();
    mc.m_mesh_asset = m_cube_mesh;
    mc.m_material_assets.push_back(desc.material);
    mc.m_is_eagerly_loaded = true;

    // 6. Scale the cube mesh to match the collision box.
    // The builtin cube is 1x1x1, so the scale in each axis is half_extents * 2.
    go.GetTransformRef().SetScale(desc.half_extents * 2.0f);

    // 7. Track for PreRenderUpdate.
    m_mesh_components.push_back(&mc);

    return go;
}

std::vector<StaticMeshComponent *> &SceneBuilder::GetMeshComponents() {
    return m_mesh_components;
}

void SceneBuilder::Finalize(PhysicsScene &physics_scene) {
    physics_scene.InitializePendingRigidBodies(m_render_system);
}
