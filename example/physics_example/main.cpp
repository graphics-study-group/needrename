#include "CameraControllerComponent.h"
#include "PhysicsExampleRenderGraphBuilder.h"
#include "SimulationToggleComponent.h"

#include "Asset/AssetDatabase/FileSystemDatabase.h"
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
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "UserInterface/Input.h"
#include "cmake_config.h"

#include <SDL3/SDL.h>
#include <cassert>

using namespace Engine;

namespace {

    /**
     * @brief Helper to add a sphere StaticMeshComponent for visualization.
     * @return Reference to the created StaticMeshComponent.
     */
    StaticMeshComponent &AddSphereMesh(GameObject &obj, FileSystemDatabase &adb) {
        AssetRef sphere_mesh = adb.GetNewAssetRef(AssetPath{adb, "/Sphere.asset"});
        AssetRef pbr_material = adb.GetNewAssetRef(AssetPath{adb, "/red_brick_sphere_512_default_pbr.asset"});

        auto &mc = obj.AddComponent<StaticMeshComponent>();
        mc.m_mesh_asset = sphere_mesh;
        mc.m_material_assets.push_back(pbr_material);
        mc.m_is_eagerly_loaded = true;
        obj.GetTransformRef().SetScale(glm::vec3(0.3f));
        return mc;
    }

} // namespace

int main(int /*argc*/, char ** /*argv*/) {
    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Physics Example"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    auto &world_system = *cmc->GetWorldSystem();
    Scene &scene = world_system.GetMainSceneRef();
    auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());

    // --- Set up input axes ---
    auto input = cmc->GetInputSystem();
    input->AddAxis(Input::ButtonAxis("move forward", Input::AxisType::TypeKey, "w", "s"));
    input->AddAxis(Input::ButtonAxis("move right", Input::AxisType::TypeKey, "d", "a"));
    input->AddAxis(Input::ButtonAxis("move up", Input::AxisType::TypeKey, "e", "q"));
    input->AddAxis(
        Input::MotionAxis("look x", Input::AxisType::TypeMouseMotion, "x", 0.3f, 3.0f, 0.001f, 3.0f, false, true)
    );
    input->AddAxis(
        Input::MotionAxis("look y", Input::AxisType::TypeMouseMotion, "y", 0.3f, 3.0f, 0.001f, 3.0f, false, true)
    );
    input->AddAxis(Input::ButtonAxis("mouse right", Input::AxisType::TypeMouseButton, "mouse right", ""));
    input->AddAxis(Input::ButtonAxis("toggle simulation", Input::AxisType::TypeKey, "space", ""));

    // Track all StaticMeshComponents for PreRenderUpdate.
    std::vector<StaticMeshComponent *> mesh_components;

    // --- Create physics objects ---
    // Root object to hold the physics scene hierarchy.
    GameObject &root = scene.CreateGameObject();
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        root.SetTransform(t);
    }

    // Ground: a large kinematic box.
    GameObject &ground = scene.CreateGameObject();
    ground.SetParent(root.GetHandle());
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, -2.0f));
        ground.SetTransform(t);
    }
    ground.AddComponent<RigidBodyComponent>().m_is_kinematic = true;
    auto &ground_shape = ground.AddComponent<CollisionShapeComponent>();
    ground_shape.m_box_size = glm::vec3(10.0f, 10.0f, 2.0f);
    mesh_components.push_back(&AddSphereMesh(ground, adb));
    ground.GetTransformRef().SetScale(glm::vec3(10.0f, 10.0f, 2.0f));

    // Falling boxes stacked in a pyramid.
    struct BoxSpec {
        float x, y, z;
    };
    const BoxSpec boxes[] = {
        {0.0f, 0.0f, 2.0f},
        {-1.5f, -1.5f, 2.0f},
        {1.5f, -1.5f, 2.0f},
        {-1.5f, 1.5f, 2.0f},
        {1.5f, 1.5f, 2.0f},
        {0.0f, 0.0f, 5.0f},
    };

    for (const auto &box : boxes) {
        GameObject &go = scene.CreateGameObject();
        go.SetParent(root.GetHandle());
        {
            Transform t;
            t.SetPosition(glm::vec3(box.x, box.y, box.z));
            go.SetTransform(t);
        }
        go.AddComponent<RigidBodyComponent>();
        auto &shape = go.AddComponent<CollisionShapeComponent>();
        shape.m_box_size = glm::vec3(0.5f, 0.5f, 0.5f);
        mesh_components.push_back(&AddSphereMesh(go, adb));
    }

    // --- Camera setup ---
    GameObject &camera_object = scene.CreateGameObject();
    camera_object.SetParent(root.GetHandle());
    {
        Transform t;
        t.SetPosition(glm::vec3(8.0f, -12.0f, 6.0f));
        // Compute a quaternion that rotates the camera forward (Y+) toward the
        // scene center.  Camera::UpdateViewMatrix builds the view matrix as
        //   eye + transform.rotation * (0,1,0)  → forward = local Y+.
        glm::vec3 look_dir = glm::normalize(glm::vec3(0.0f, 0.0f, 2.0f) - t.GetPosition());
        glm::vec3 fwd(0.0f, 1.0f, 0.0f);
        float dot = glm::dot(fwd, look_dir);
        glm::quat look_rot;
        if (dot > 0.9999f) {
            look_rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // already facing target
        } else if (dot < -0.9999f) {
            look_rot = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)); // 180° about Z
        } else {
            float angle = glm::acos(dot);
            glm::vec3 axis = glm::normalize(glm::cross(fwd, look_dir));
            look_rot = glm::angleAxis(angle, axis);
        }
        t.SetRotation(look_rot);
        camera_object.SetTransform(t);
    }
    camera_object.AddComponent<CameraComponent>();
    camera_object.AddComponent<CameraControllerComponent>();

    // Simulation toggle on the camera object (convenient, always alive).
    camera_object.AddComponent<SimulationToggleComponent>();

    // --- Finalize scene ---
    scene.FlushCmdQueue();

    PhysicsScene *physics_scene = scene.GetPhysicsScene();
    if (physics_scene == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene is null.");
        return -1;
    }

    physics_scene->InitializePendingRigidBodies(*cmc->GetRenderSystem());
    physics_scene->DebugPrint();

    // Awake mesh components -> registers renderers with RendererManager.
    scene.AddInitEvent();
    scene.ProcessEvents();

    // Set model_mat_index for physics-driven renderers.
    for (auto *mc : mesh_components) {
        mc->PreRenderUpdate();
    }

    // --- Build the combined physics + rendering render graph ---
    PhysicsExampleRenderGraphBuilder rg_builder(*cmc->GetRenderSystem());
    RGTextureHandle final_color_id;
    auto rg = rg_builder.BuildRenderGraph(1280, 720, *physics_scene, final_color_id);
    cmc->SetRenderGraph(std::move(rg), final_color_id);

    // --- Infinite interactive loop ---
    // Simulation starts paused (m_simulation_enabled defaults to false).
    // Press SPACE to toggle.
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop. Press SPACE to toggle simulation.");
    cmc->MainLoop();

    return 0;
}
