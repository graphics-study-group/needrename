#include "CameraControllerComponent.h"
#include "PhysicsExampleRenderGraphBuilder.h"
#include "SceneBuilder.h"
#include "SimulationToggleComponent.h"

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/LightComponent.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "Framework/component/TransformComponent/TransformComponent.h"
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

int main(int /*argc*/, char ** /*argv*/) {
    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "empty_with_sky";

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

    // --- Load preset solid color materials ---
    auto red_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_red.asset"});
    auto green_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_green.asset"});
    auto blue_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_blue.asset"});
    auto yellow_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_yellow.asset"});
    auto cyan_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_cyan.asset"});
    auto magenta_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_magenta.asset"});
    auto orange_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_orange.asset"});
    auto white_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_white.asset"});

    // --- Create physics objects via SceneBuilder ---
    // Root object to hold the physics scene hierarchy.
    GameObject &root = scene.CreateGameObject();
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        root.SetTransform(t);
    }

    SceneBuilder builder(scene, adb, root, *cmc->GetRenderSystem());

    // Two slightly overlapping boxes for collision detection testing.
    // Default half_extents is {0.5, 0.5, 0.5}, so each box is 1x1x1.
    // Box 1 at (0,0,0) spans [-0.5, 0.5], Box 2 at (0.5,0.5,0.5) spans [0.0, 1.0].
    // Overlap is ~0.5 units on all three axes — clearly detectable.
    builder.AddBox({
        .position = {0.0f, 0.0f, 0.0f},
        .half_extents = {1.0f, 1.0f, 1.0f},
        .material = red_mat,
    });
    builder.AddBox({
        .position = {0.0f, 0.0f, 1.59f},
        .rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .half_extents = {0.6f, 0.6f, 0.6f},
        .material = blue_mat,
    });

    // --- Camera setup ---
    GameObject &camera_object = scene.CreateGameObject();
    camera_object.SetParent(root.GetHandle());
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, -4.0f, 2.0f));
        // Compute a quaternion that rotates the camera forward (Y+) toward the
        // scene center.  Camera::UpdateViewMatrix builds the view matrix as
        //   eye + transform.rotation * (0,1,0)  → forward = local Y+.
        glm::vec3 look_dir = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f) - t.GetPosition());
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
    auto &camera_comp = camera_object.AddComponent<CameraComponent>();
    camera_comp.m_camera->set_aspect_ratio(1.0f * opt.resol_x / opt.resol_y);
    camera_object.AddComponent<CameraControllerComponent>();

    // Simulation toggle on the camera object (convenient, always alive).
    camera_object.AddComponent<SimulationToggleComponent>();

    // Register our camera as the active camera.
    // Must be called so WorldSystem::GetActiveCamera() returns a valid pointer,
    // which the lit pass (and CameraComponent::Tick) depend on.
    cmc->GetWorldSystem()->SetActiveCamera(camera_comp.GetHandle(), &cmc->GetRenderSystem()->GetCameraManager());

    // --- Light setup ---
    GameObject &light_object1 = scene.CreateGameObject();
    {
        auto &light_comp = light_object1.AddComponent<LightComponent>();
        light_comp.m_cast_shadow = true;
        light_comp.m_type = LightType::Directional;
        light_comp.m_intensity = 2.0f;
        Transform t;
        t.SetRotation(glm::quat(glm::vec3(glm::radians(-60.0f), glm::radians(-60.0f), 0.0f)));
        light_object1.SetTransform(t);
    }
    GameObject &light_object2 = scene.CreateGameObject();
    {
        auto &light_comp = light_object2.AddComponent<LightComponent>();
        light_comp.m_cast_shadow = true;
        light_comp.m_type = LightType::Directional;
        light_comp.m_intensity = 2.0f;
        Transform t;
        t.SetRotation(glm::quat(glm::vec3(glm::radians(-120.0f), glm::radians(60.0f), 0.0f)));
        light_object2.SetTransform(t);
    }

    // --- Finalize scene ---
    scene.FlushCmdQueue();

    PhysicsScene *physics_scene = scene.GetPhysicsScene();
    if (physics_scene == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene is null.");
        return -1;
    }

    builder.Finalize(*physics_scene);
    physics_scene->DebugPrint();

    // Awake mesh components -> registers renderers with RendererManager.
    scene.AddInitEvent();
    scene.ProcessEvents();

    // Set model_mat_index for physics-driven renderers.
    for (auto *mc : builder.GetMeshComponents()) {
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
