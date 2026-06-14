#include "CameraControllerComponent.h"
#include "PhysicsExampleRenderGraphBuilder.h"
#include "SceneBuilder.h"
#include "SimulationToggleComponent.h"

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
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

    // --- Load preset solid color materials ---
    auto red_mat    = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_red.asset"});
    auto green_mat  = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_green.asset"});
    auto blue_mat   = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_blue.asset"});
    auto yellow_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_yellow.asset"});
    auto cyan_mat   = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_cyan.asset"});
    auto magenta_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_magenta.asset"});
    auto orange_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_orange.asset"});
    auto white_mat  = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_white.asset"});
    auto grey_mat   = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_dark_grey_solid.asset"});

    // --- Create physics objects via SceneBuilder ---
    // Root object to hold the physics scene hierarchy.
    GameObject &root = scene.CreateGameObject();
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        root.SetTransform(t);
    }

    SceneBuilder builder(scene, adb, root, *cmc->GetRenderSystem());

    // Ground: a large kinematic box.
    builder.AddBox({
        .position = {0.0f, 0.0f, -2.0f},
        .half_extents = {10.0f, 10.0f, 2.0f},
        .kinematic = true,
        .material = grey_mat,
    });

    // Falling boxes stacked in a pyramid, cycling through preset colors.
    const std::vector<AssetRef> preset_colors = {
        red_mat, green_mat, blue_mat, yellow_mat, cyan_mat, magenta_mat, orange_mat, white_mat
    };
    struct BoxPos { float x, y, z; };
    const BoxPos box_positions[] = {
        {0.0f, 0.0f, 2.0f},
        {-1.5f, -1.5f, 2.0f},
        {1.5f, -1.5f, 2.0f},
        {-1.5f, 1.5f, 2.0f},
        {1.5f, 1.5f, 2.0f},
        {0.0f, 0.0f, 5.0f},
    };

    for (size_t i = 0; i < std::size(box_positions); i++) {
        const auto &pos = box_positions[i];
        builder.AddBox({
            .position = {pos.x, pos.y, pos.z},
            .material = preset_colors[i % preset_colors.size()],
        });
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
    auto &camera_comp = camera_object.AddComponent<CameraComponent>();
    camera_comp.m_camera->set_aspect_ratio(1.0f * opt.resol_x / opt.resol_y);
    camera_object.AddComponent<CameraControllerComponent>();

    // Simulation toggle on the camera object (convenient, always alive).
    camera_object.AddComponent<SimulationToggleComponent>();

    // Register our camera as the active camera.
    // Must be called so WorldSystem::GetActiveCamera() returns a valid pointer,
    // which the lit pass (and CameraComponent::Tick) depend on.
    cmc->GetWorldSystem()->SetActiveCamera(camera_comp.GetHandle(), &cmc->GetRenderSystem()->GetCameraManager());

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
