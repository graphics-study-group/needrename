#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Core/Delegate/FuncDelegate.h>
#include <Core/Functional/EventQueue.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <Framework/component/Component.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/FullRenderSystem.h>
#include <SDL3/SDL.h>
#include <UserInterface/GUISystem.h>
#include <UserInterface/Input.h>
#include <cmake_config.h>

#include <Editor/Widget/GameWidget.h>
#include <Editor/Widget/SceneWidget.h>
#include <Editor/Window/MainWindow.h>
#include <Editor/Render/EditorRenderGraphBuilder.h>

#include "CustomComponent.h"
#include <meta_editor_run_game_example/reflection_init.ipp>

using namespace Engine;

SpinningComponent::SpinningComponent(GameObject *parent) : Component(parent) {
}

void SpinningComponent::Init() {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SpinningComponent Init");
}

void SpinningComponent::Tick() {
    float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
    auto go = m_scene->GetGameObject(m_parentGameObject);
    if (go) {
        auto &transform = go->GetTransformRef();
        transform.SetRotation(
            transform.GetRotation() * glm::angleAxis(glm::radians(m_speed * dt), glm::vec3(0.0f, 0.0f, 1.0f))
        );
    }
}

ControlComponent::ControlComponent(GameObject *parent) : Component(parent) {
}

void ControlComponent::Tick() {
    auto input = MainClass::GetInstance()->GetInputSystem();
    auto move_forward = input->GetAxis("move forward");
    auto move_backward = input->GetAxis("move backward");
    auto move_right = input->GetAxis("move right");
    auto move_up = input->GetAxis("move up");
    auto roll_right = input->GetAxisRaw("roll right");
    auto look_x = input->GetAxisRaw("look x");
    auto look_y = input->GetAxisRaw("look y");
    Transform &transform = m_scene->GetGameObjectRef(m_parentGameObject).GetTransformRef();
    float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
    transform.SetRotation(
        transform.GetRotation()
        * glm::quat(
            glm::vec3{look_y * m_rotation_speed * dt, roll_right * m_roll_speed * dt, look_x * m_rotation_speed * dt}
        )
    );
    transform.SetPosition(
        transform.GetPosition()
        + transform.GetRotation() * glm::vec3{move_right, move_forward + move_backward, move_up} * m_move_speed * dt
    );
}

void Start() {
    auto cmc = MainClass::GetInstance();
    auto &scene = cmc->GetWorldSystem()->GetMainSceneRef();
    scene.ClearEventQueue();
    scene.AddInitEvent();
}

int main() {
    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

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
    SDL_Log("Screen Resolution: %dx%d @ %fHz", 
            screenWidth, screenHeight, displayMode->refresh_rate);
    StartupOptions opt{.resol_x = (int)(screenWidth * 0.9), .resol_y = (int)(screenHeight * 0.9), .title = "Editor"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    RegisterAllTypes();
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    auto rsys = cmc->GetRenderSystem();
    auto asys = cmc->GetAssetManager();
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    auto world = cmc->GetWorldSystem();
    auto gui = cmc->GetGUISystem();
    auto window = cmc->GetWindow();
    auto &main_scene = world->GetMainSceneRef();
    gui->CreateVulkanBackend(*rsys, ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Add extra objects");
    auto input = MainClass::GetInstance()->GetInputSystem();
    input->AddAxis(Input::ButtonAxis("move forward", Input::AxisType::TypeKey, "w", "s"));
    input->AddAxis(Input::ButtonAxis("move right", Input::AxisType::TypeKey, "d", "a"));
    input->AddAxis(Input::ButtonAxis("move up", Input::AxisType::TypeKey, "space", "left shift"));
    input->AddAxis(Input::ButtonAxis("roll right", Input::AxisType::TypeKey, "e", "q"));
    input->AddAxis(
        Input::MotionAxis("look x", Input::AxisType::TypeMouseMotion, "x", 0.3f, 3.0f, 0.001f, 3.0f, false, true)
    );
    input->AddAxis(
        Input::MotionAxis("look y", Input::AxisType::TypeMouseMotion, "y", 0.3f, 3.0f, 0.001f, 3.0f, false, true)
    );

    auto &camera_go = main_scene.CreateGameObject();
    camera_go.m_name = "Controled Camera";
    Transform transform{};
    transform.SetPosition({0.0f, -0.7f, 0.5f});
    transform.SetRotationEuler(glm::vec3{glm::radians(-30.0f), 0.0, 0.0});
    transform.SetScale({1.0f, 1.0f, 1.0f});
    camera_go.SetTransform(transform);
    auto &camera_comp = camera_go.template AddComponent<CameraComponent>();
    camera_comp.m_camera->set_aspect_ratio(1.0 * opt.resol_x / opt.resol_y);
    camera_go.template AddComponent<ControlComponent>();
    world->SetActiveCamera(camera_comp.GetHandle(), &cmc->GetRenderSystem()->GetCameraManager());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Create Editor Window");
    Editor::MainWindow main_window;
    auto scene_widget = std::make_shared<Editor::SceneWidget>(Editor::MainWindow::k_scene_widget_name);
    main_window.AddWidget(scene_widget);
    auto game_widget = std::make_shared<Editor::GameWidget>(Editor::MainWindow::k_game_widget_name);
    main_window.AddWidget(game_widget);

    auto rgb = std::make_unique<Editor::EditorRenderGraphBuilder>(*cmc->GetRenderSystem());
    int32_t final_color_id, scene_color_id, game_color_id;
    auto rg = rgb->BuildEditorRenderGraph(
        screenWidth,
        screenHeight,
        [scene_widget]() -> vk::Extent2D { return {scene_widget->m_viewport_size.x, scene_widget->m_viewport_size.y}; },
        [scene_widget]() -> uint8_t { return scene_widget->GetCameraIndex(); },
        [game_widget]() -> vk::Extent2D { return {game_widget->m_viewport_size.x, game_widget->m_viewport_size.y}; },
        [world]() -> uint8_t { return world->GetActiveCamera()->m_display_id; },
        gui.get(),
        scene_color_id,
        game_color_id,
        final_color_id
    );

    auto scene_texture = rg->GetInternalTextureResource(scene_color_id);
    scene_widget->SetDisplayTexture(*scene_texture);
    auto game_texture = rg->GetInternalTextureResource(game_color_id);
    game_widget->SetDisplayTexture(*game_texture);

    main_window.m_OnStart.AddDelegate(std::make_unique<FuncDelegate<>>(Start));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");

    bool onQuit = false;
    while (!onQuit) {
        cmc->GetTimeSystem()->NextFrame();

        asys->LoadAssetsInQueue();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                onQuit = true;
                break;
            }
            gui->ProcessEvent(&event);
            if (game_widget->m_accept_input) cmc->GetInputSystem()->ProcessEvent(&event);
        }

        if (game_widget->m_accept_input) cmc->GetInputSystem()->Update();
        else cmc->GetInputSystem()->ResetAxes();
        world->GetMainSceneRef().FlushCmdQueue();

        if (main_window.m_is_playing) {
            world->GetMainSceneRef().AddTickEvent();
            world->GetMainSceneRef().ProcessEvents();
        }
        world->UpdateLightData(rsys->GetSceneDataManager());

        gui->PrepareGUI();
        main_window.Render();

        rsys->StartFrame();
        rg->Execute();
        auto [w, h] = cmc->GetWindow()->GetSize();
        rsys->CompleteFrame(*rg->GetInternalTextureResource(final_color_id), w, h);
    }
    rsys->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");

    return 0;
}
