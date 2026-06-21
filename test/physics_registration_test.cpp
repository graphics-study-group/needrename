#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Core/Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "Framework/component/TransformComponent/TransformComponent.h"
#include "Framework/component/physics/CollisionShapeComponent.h"
#include "Framework/component/physics/RigidBodyComponent.h"
#include "Framework/object/GameObject.h"
#include "Framework/world/Scene.h"
#include "Framework/world/WorldSystem.h"
#include "MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/Solver/XPBDGpuSolver.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"
#include "cmake_config.h"

#include <SDL3/SDL.h>
#include <cassert>

using namespace Engine;

int main(int argc, char **argv) {
    int64_t max_frame_count = 300;
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Physics Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);
    auto &world_system = *cmc->GetWorldSystem();
    Scene &scene = world_system.GetMainSceneRef();

    GameObject &root = scene.CreateGameObject();
    GameObject &child = scene.CreateGameObject();
    GameObject &child_rigidbody_root = scene.CreateGameObject();
    GameObject &child_rigidbody_grandchild = scene.CreateGameObject();
    GameObject &loose = scene.CreateGameObject();

    child.SetParent(root.GetHandle());
    child_rigidbody_root.SetParent(root.GetHandle());
    child_rigidbody_grandchild.SetParent(child_rigidbody_root.GetHandle());

    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        root.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(2.0f, 0.0f, 0.0f));
        child.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(4.0f, 0.0f, 0.0f));
        child_rigidbody_root.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(5.0f, 0.0f, 0.0f));
        child_rigidbody_grandchild.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
        loose.SetTransform(t);
    }

    // Physics components
    root.AddComponent<RigidBodyComponent>();
    auto &root_shape = root.AddComponent<CollisionShapeComponent>();
    root_shape.m_feature = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &child_shape = child.AddComponent<CollisionShapeComponent>();
    child_shape.m_feature = glm::vec3(0.5f, 0.5f, 0.5f);

    child_rigidbody_root.AddComponent<RigidBodyComponent>();
    auto &nested_shape_root = child_rigidbody_root.AddComponent<CollisionShapeComponent>();
    nested_shape_root.m_feature = glm::vec3(0.5f, 0.5f, 0.5f);

    auto &nested_shape_grandchild = child_rigidbody_grandchild.AddComponent<CollisionShapeComponent>();
    nested_shape_grandchild.m_feature = glm::vec3(0.5f, 0.5f, 0.5f);

    auto &loose_shape = loose.AddComponent<CollisionShapeComponent>();
    loose_shape.m_feature = glm::vec3(0.5f, 0.5f, 0.5f);
    loose_shape.m_center = glm::vec3(1.0f, 0.0f, 0.0f);

    // --- Sphere and cylinder shape tests ---
    GameObject &sphere_obj = scene.CreateGameObject();
    sphere_obj.SetParent(root.GetHandle());
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 2.0f, 0.0f));
        sphere_obj.SetTransform(t);
    }
    auto &sphere_shape = sphere_obj.AddComponent<CollisionShapeComponent>();
    sphere_shape.m_shape_type = CollisionShapeType::Sphere;
    sphere_shape.m_feature = glm::vec3(1.5f, 0.0f, 0.0f); // radius = 1.5

    GameObject &cylinder_obj = scene.CreateGameObject();
    cylinder_obj.SetParent(root.GetHandle());
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 4.0f, 0.0f));
        cylinder_obj.SetTransform(t);
    }
    auto &cylinder_shape = cylinder_obj.AddComponent<CollisionShapeComponent>();
    cylinder_shape.m_shape_type = CollisionShapeType::Cylinder;
    cylinder_shape.m_feature = glm::vec3(1.0f, 0.5f, 0.0f); // radius = 1.0, half_height = 0.5

    // Add mesh components to all physics objects for visualization.
    auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    AssetRef sphere_mesh = adb.GetNewAssetRef(AssetPath{adb, "/Sphere.asset"});
    AssetRef pbr_material = adb.GetNewAssetRef(AssetPath{adb, "/red_brick.asset"});

    auto add_mesh = [&](GameObject &obj) -> StaticMeshComponent & {
        auto &mc = obj.AddComponent<StaticMeshComponent>();
        mc.m_mesh_asset = sphere_mesh;
        mc.m_material_assets.push_back(pbr_material);
        mc.m_is_eagerly_loaded = true;
        obj.GetTransformRef().SetScale(glm::vec3(0.3f));
        return mc;
    };

    auto &root_mesh = add_mesh(root);
    auto &child_mesh = add_mesh(child);
    auto &nested_root_mesh = add_mesh(child_rigidbody_root);
    auto &nested_grandchild_mesh = add_mesh(child_rigidbody_grandchild);
    auto &loose_mesh = add_mesh(loose);

    scene.FlushCmdQueue();

    PhysicsScene *physics_scene = scene.GetPhysicsScene();
    if (physics_scene == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene is null.");
        return -1;
    }

    physics_scene->InitializePendingRigidBodies(*cmc->GetRenderSystem());
    physics_scene->DebugPrint();

    // Awake mesh components → registers renderers with RendererManager.
    scene.AddInitEvent();
    scene.ProcessEvents();

    // Set model_mat_index for physics-driven renderers.
    root_mesh.PreRenderUpdate();
    child_mesh.PreRenderUpdate();
    nested_root_mesh.PreRenderUpdate();
    nested_grandchild_mesh.PreRenderUpdate();
    loose_mesh.PreRenderUpdate();

    return 0;
}
