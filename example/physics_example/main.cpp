#include "CameraControllerComponent.h"
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
#include "Framework/world/physics/PhysicsAdaptor.h"
#include "MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/Solver/XPBDGpuSolver.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/RenderGraph/ComplexRenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "UserInterface/Input.h"
#include "cmake_config.h"
#include <Framework/component/physics/PhysicsConstraintComponent.h>

#include <Framework/component/physics/RigidBodyComponent.h>

#include <SDL3/SDL.h>
#include <cassert>

using namespace Engine;

void AddTemplateScene(SceneBuilder &builder, FileSystemDatabase &adb, glm::vec3 global_offset) {
    // --- Load preset solid color materials ---
    auto red_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_red.asset"});
    auto green_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_green.asset"});
    auto blue_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_blue.asset"});
    auto yellow_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_yellow.asset"});
    auto cyan_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_cyan.asset"});
    auto magenta_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_magenta.asset"});
    auto orange_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_orange.asset"});
    auto white_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_white.asset"});

    // ---- Falling boxes (dynamic, stacked at various Z heights) ----
    // Red box — drops from center.
    builder.AddBox({
        .position = glm::vec3(0.0f, 0.0f, 0.49f) + global_offset,
        .rotation = glm::angleAxis(glm::radians(0.0f), glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f))),
        .half_extents = {0.5f, 0.5f, 0.5f},
        .mass = 1.0f,
        .material = red_mat,
    });

    builder.AddBox({
        .position = glm::vec3(0.0f, -0.7f, 2.0f) + global_offset,
        .rotation = glm::angleAxis(glm::radians(44.0f), glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))),
        .half_extents = {0.2f, 0.5f, 0.2f},
        .mass = 1.0f,
        .material = green_mat,
    });

    // Green box — offset in X, higher up.
    builder.AddSphere({
        .position = glm::vec3(1.2f, 0.0f, 7.0f) + global_offset,
        .radius = 0.5f,
        .mass = 1.0f,
        .material = green_mat,
    });

    // Blue box — offset in Y, medium height.
    builder.AddCylinder({
        .position = glm::vec3(0.0f, 1.2f, 6.0f) + global_offset,
        .radius = 0.2f,
        .half_height = 1.0f,
        .mass = 1.0f,
        .material = blue_mat,
    });

    // Yellow box — taller shape, higher up.
    builder.AddSphere({
        .position = glm::vec3(-1.0f, -0.5f, 8.0f) + global_offset,
        .radius = 0.4f,
        .mass = 2.0f,
        .material = yellow_mat,
    });

    // Cyan box — wide flat box.
    builder.AddCylinder({
        .position = glm::vec3(2.0f, -1.0f, 9.0f) + global_offset,
        .radius = 0.8f,
        .half_height = 0.3f,
        .mass = 0.5f,
        .material = cyan_mat,
    });

    // Magenta box — small cube, highest.
    builder.AddCylinder({
        .position = glm::vec3(-2.0f, 1.0f, 10.0f) + global_offset,
        .rotation = glm::angleAxis(glm::radians(90.0f), glm::normalize(glm::vec3(0.5f, 0.0f, 1.0f))),
        .radius = 0.3f,
        .half_height = 0.6f,
        .mass = 0.3f,
        .material = magenta_mat,
    });

    // Orange box — medium cube, slightly rotated.
    builder.AddBox({
        .position = glm::vec3(0.5f, 2.0f, 4.0f) + global_offset,
        .rotation = glm::angleAxis(glm::radians(25.0f), glm::normalize(glm::vec3(0.3f, 0.2f, 0.7f))),
        .half_extents = {0.5f, 0.5f, 0.5f},
        .mass = 1.5f,
        .material = orange_mat,
    });

    // rigid bricks
    int n = 6;
    glm::vec3 brick_size(0.5f, 0.8f, 0.5f);
    glm::vec3 offset(5.0f, 0.7f, 0.0f);
    offset += global_offset;
    for (int i = 0; i < n; ++i) {
        glm::vec3 start_pos = glm::vec3(0.0f, -0.5f * brick_size.y * n, brick_size.z * (i + 0.5f)) + offset;
        for (int j = 0; j < n - i; ++j)
            builder.AddBox({
                .position = start_pos + glm::vec3(0.0f, brick_size.y * (j + 0.5f * i), 0.0f),
                .half_extents = brick_size * 0.5f,
                .mass = 0.2f,
                .material = blue_mat,
            });
    }

    // Double pendulum demo — hinge + fixed joint test.
    builder.AddDoublePendulum(glm::vec3(6.5f, 0.0f, 4.5f) + global_offset);
}

void AddTemplateScene2(SceneBuilder &builder, FileSystemDatabase &adb, glm::vec3 global_offset) {
    // --- Load preset solid color materials ---
    auto red_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_red.asset"});
    auto green_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_green.asset"});
    auto blue_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_blue.asset"});
    auto yellow_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_yellow.asset"});
    auto cyan_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_cyan.asset"});
    auto magenta_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_magenta.asset"});
    auto orange_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_orange.asset"});
    auto white_mat = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_white.asset"});

    float wall_half_height = 3.0f;
    float wall_half_size = 15.0f;
    builder.AddBox(
        {.position = glm::vec3(-wall_half_size, 0.0f, wall_half_height) + global_offset,
         .rotation = glm::quat(),
         .half_extents = {0.5f, wall_half_size, wall_half_height},
         .mass = 1.0f,
         .kinematic = true,
         .material = blue_mat}
    );
    builder.AddBox(
        {.position = glm::vec3(wall_half_size, 0.0f, wall_half_height) + global_offset,
         .rotation = glm::quat(),
         .half_extents = {0.5f, wall_half_size, wall_half_height},
         .mass = 1.0f,
         .kinematic = true,
         .material = blue_mat}
    );
    builder.AddBox(
        {.position = glm::vec3(0.0f, -wall_half_size, wall_half_height) + global_offset,
         .rotation = glm::quat(),
         .half_extents = {wall_half_size, 0.5f, wall_half_height},
         .mass = 1.0f,
         .kinematic = true,
         .material = blue_mat}
    );
    builder.AddBox(
        {.position = glm::vec3(0.0f, wall_half_size, wall_half_height) + global_offset,
         .rotation = glm::quat(),
         .half_extents = {wall_half_size, 0.5f, wall_half_height},
         .mass = 1.0f,
         .kinematic = true,
         .material = blue_mat}
    );

    auto &obj = builder.AddBox(
        {.position = glm::vec3(0.0f, 0.0f, 0.7f) + global_offset,
         .rotation = glm::quat(),
         .half_extents = {0.3f, 8.0f, 0.7f},
         .mass = 1.0f,
         .kinematic = true,
         .material = orange_mat}
    );
    for (auto &comp : obj.m_components) {
        if (auto *tc = dynamic_cast<RigidBodyComponent *>(comp.GetComponent())) {
            tc->m_angular_velocity_axis_angle = glm::vec3(0.0f, 0.0f, 1.0f) * glm::radians(90.0f);
            break;
        }
    }

    int n = 9;
    int m = 15;
    float offset = 2.2f;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            for (int k = 0; k < m; k++) {
                int type = rand() % 3;
                glm::vec3 pos =
                    glm::vec3((-(n + 1) / 2 + i) * offset, (-(n + 1) / 2 + j) * offset, ((n + 1) / 2 + k) * offset)
                    + global_offset;
                glm::vec3 rot_axis = glm::normalize(glm::vec3(rand() % 100, rand() % 100, rand() % 100));
                float rot_angle = glm::radians((float)(rand() % 360));
                glm::quat rot = glm::angleAxis(rot_angle, rot_axis);
                switch (type) {
                case 0:
                    builder.AddBox(
                        {.position = pos,
                         .rotation = rot,
                         .half_extents = {0.5f, 0.7f, 0.5f},
                         .mass = 1.0f,
                         .material = red_mat}
                    );
                    break;
                case 1:
                    builder.AddSphere(
                        {.position = pos, .rotation = rot, .radius = 0.5f, .mass = 1.0f, .material = green_mat}
                    );
                    break;
                case 2:
                    builder.AddCylinder(
                        {.position = pos,
                         .rotation = rot,
                         .radius = 0.5f,
                         .half_height = 0.5f,
                         .mass = 1.0f,
                         .material = blue_mat}
                    );
                    break;
                default:
                    break;
                }
            }
}

int main(int /*argc*/, char ** /*argv*/) {
    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "empty_with_sky";

    SDL_Init(SDL_INIT_VIDEO);

    int displayIndex = 1;
    auto displayMode = SDL_GetDesktopDisplayMode(displayIndex);
    if (displayMode == nullptr) {
        SDL_Log("Failed to get display mode: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    int screenWidth = displayMode->w;
    int screenHeight = displayMode->h;
    SDL_Log("Screen Resolution: %dx%d @ %fHz", screenWidth, screenHeight, displayMode->refresh_rate);
    StartupOptions opt{
        .resol_x = (int)(screenWidth * 0.9), .resol_y = (int)(screenHeight * 0.9), .title = "Physics Example"
    };

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

    // --- Create physics objects via SceneBuilder ---
    GameObject &root = scene.CreateGameObject();
    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        root.SetTransform(t);
    }

    SceneBuilder builder(scene, adb, root);

    // ---- Ground plane (kinematic, large flat box in XY, thin in Z) ----
    // Z is up, so the floor extends in X and Y, centered at z = -0.5.
    builder.AddBox({
        .position = {0.0f, 0.0f, -0.5f},
        .half_extents = {100.0f, 100.0f, 0.5f},
        .mass = 0.0f,
        .kinematic = true,
        .material = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_white.asset"}),
    });

    // AddTemplateScene(builder, adb, glm::vec3(0.0f, 5.0f, 0.0f));
    int n = 7;
    float margin = 8.0f;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            AddTemplateScene(builder, adb, glm::vec3((i - n / 2.0f) * margin, (j - n / 2.0f) * margin, 0.0f));
    // AddTemplateScene2(builder, adb, glm::vec3(0.0f, 0.0f, 0.0f));

    // {
    //     auto &box1 = builder.AddBox({
    //         .position = {0.0f, 0.0f, 3.0f},
    //         .half_extents = {2.0f, 0.5f, 0.5f},
    //         .mass = 6.0f,
    //         .kinematic = false,
    //         .material = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_blue.asset"}),
    //     });

    //     auto &box2 = builder.AddCylinder({
    //         .position = {2.6f, 0.0f, 3.0f},
    //         .radius = 0.5f,
    //         .half_height = 2.0f,
    //         .mass = 0.1f,
    //         .kinematic = false,
    //         .material = adb.GetNewAssetRef(AssetPath{adb, "~/materials/solid_color_red.asset"}),
    //     });

    //     auto &constraint = box2.AddComponent<PhysicsConstraintComponent>();
    //     FixedJointDef fixed{};
    //     fixed.m_obj2_handle = box1.GetHandle();
    //     fixed.m_compliance = 0.0f;
    //     constraint.m_joints.push_back(fixed);
    // }

    // --- Camera setup ---
    // Z is up, camera is positioned to the side looking at the falling zone.
    GameObject &camera_object = scene.CreateGameObject();
    camera_object.SetParent(root.GetHandle());
    {
        Transform t;
        t.SetPosition(glm::vec3(6.0f, -5.0f, 4.0f));
        // Look at scene center — roughly where the action is.
        glm::vec3 look_target(0.0f, 0.0f, 3.0f);
        glm::vec3 look_dir = glm::normalize(look_target - t.GetPosition());
        glm::vec3 fwd(0.0f, 1.0f, 0.0f);
        float dot = glm::dot(fwd, look_dir);
        glm::quat look_rot;
        if (dot > 0.9999f) {
            look_rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        } else if (dot < -0.9999f) {
            look_rot = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
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

    // Simulation toggle on the camera object (SPACE to pause/resume).
    camera_object.AddComponent<SimulationToggleComponent>();

    cmc->GetWorldSystem()->SetActiveCamera(camera_comp.GetHandle(), &cmc->GetRenderSystem()->GetCameraManager());

    // --- Light setup ---
    {
        GameObject &light_obj = scene.CreateGameObject();
        auto &light_comp = light_obj.AddComponent<LightComponent>();
        light_comp.m_cast_shadow = true;
        light_comp.m_type = LightType::Directional;
        light_comp.m_intensity = 2.0f;
        Transform t;
        t.SetRotation(glm::quat(glm::vec3(glm::radians(-45.0f), glm::radians(-30.0f), 0.0f)));
        light_obj.SetTransform(t);
    }
    {
        GameObject &light_obj = scene.CreateGameObject();
        auto &light_comp = light_obj.AddComponent<LightComponent>();
        light_comp.m_cast_shadow = true;
        light_comp.m_type = LightType::Directional;
        light_comp.m_intensity = 1.5f;
        Transform t;
        t.SetRotation(glm::quat(glm::vec3(glm::radians(-100.0f), glm::radians(45.0f), 0.0f)));
        light_obj.SetTransform(t);
    }

    // --- Finalize scene ---
    scene.FlushCmdQueue();

    auto *physics_scene = scene.GetPhysicsScene();
    auto &physics_adaptor = scene.GetPhysicsAdaptor();
    physics_scene->DebugPrint();
    physics_scene->SetSimulationEnabled(false);
    physics_adaptor.SetPhysicsActive(true);

    // Awake mesh components → registers renderers.
    scene.AddInitEvent();
    scene.ProcessEvents();
    scene.FlushPhysics(*cmc->GetRenderSystem());

    // --- Build the rendering render graph (physics model matrices passed via ComplexRenderGraphBuilder) ---
    ComplexRenderGraphBuilder rg_builder(*cmc->GetRenderSystem());
    RGTextureHandle final_color_id;
    auto mm_buf = physics_scene->GetGpuBuffers().model_matrices;
    auto rg = rg_builder.BuildDefaultRenderGraph(screenWidth, screenHeight, final_color_id, mm_buf);
    cmc->SetRenderGraph(std::move(rg), final_color_id);

    // --- Main loop ---
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop. Press SPACE to toggle simulation.");
    cmc->MainLoop();

    return 0;
}
