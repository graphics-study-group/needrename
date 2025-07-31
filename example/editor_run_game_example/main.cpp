#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>

#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Scene/GameObjectAsset.h>
#include <Core/Delegate/FuncDelegate.h>
#include <Framework/component/Component.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Framework/world/WorldSystem.h>
#include <Functional/EventQueue.h>
#include <Functional/SDLWindow.h>
#include <Functional/Time.h>
#include <GUI/GUISystem.h>
#include <Input/Input.h>
#include <MainClass.h>
#include <Render/FullRenderSystem.h>
#include <SDL3/SDL.h>
#include <cmake_config.h>

#include <Editor/Widget/GameWidget.h>
#include <Editor/Widget/SceneWidget.h>
#include <Editor/Window/MainWindow.h>

using namespace Engine;

class SpinningComponent : public Component {
public:
    SpinningComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {
    }

    float m_speed = 30.0f;

    void Init() override {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SpinningComponent Init");
    }

    void Tick() override {
        float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
        auto go = m_parentGameObject.lock();
        if (go) {
            auto &transform = go->GetTransformRef();
            transform.SetRotation(
                transform.GetRotation() * glm::angleAxis(glm::radians(m_speed * dt), glm::vec3(0.0f, 1.0f, 0.0f))
            );
        }
    }
};

class ControlComponent : public Component {
public:
    ControlComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {
    }

    std::shared_ptr<CameraComponent> m_camera{};
    float m_rotation_speed = 10.0f;
    float m_move_speed = 1.0f;
    float m_roll_speed = 1.0f;

    virtual void Tick() override {
        auto input = MainClass::GetInstance()->GetInputSystem();
        auto move_forward = input->GetAxis("move forward");
        auto move_backward = input->GetAxis("move backward");
        auto move_right = input->GetAxis("move right");
        auto move_up = input->GetAxis("move up");
        auto roll_right = input->GetAxisRaw("roll right");
        auto look_x = input->GetAxisRaw("look x");
        auto look_y = input->GetAxisRaw("look y");
        Transform &transform = m_parentGameObject.lock()->GetTransformRef();
        float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
        transform.SetRotation(
            transform.GetRotation()
            * glm::quat(
                glm::vec3{
                    look_y * m_rotation_speed * dt, roll_right * m_roll_speed * dt, look_x * m_rotation_speed * dt
                }
            )
        );
        transform.SetPosition(
            transform.GetPosition()
            + transform.GetRotation() * glm::vec3{move_right, move_forward + move_backward, move_up} * m_move_speed * dt
        );
    }
};

void Start() {
    auto cmc = MainClass::GetInstance();
    auto world = cmc->GetWorldSystem();
    auto event_queue = cmc->GetEventQueue();
    event_queue->Clear();
    world->AddInitEvent();
}

int main() {
    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Editor"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->GetAssetManager()->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    cmc->GetAssetManager()->LoadBuiltinAssets();
    auto rsys = cmc->GetRenderSystem();
    auto asset_manager = cmc->GetAssetManager();
    auto world = cmc->GetWorldSystem();
    auto gui = cmc->GetGUISystem();
    auto window = cmc->GetWindow();
    auto event_queue = cmc->GetEventQueue();
    gui->CreateVulkanBackend(ImageUtils::GetVkFormat(window->GetColorTexture().GetTextureDescription().format));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    std::filesystem::path mesh_path(ENGINE_ASSETS_DIR);
    mesh_path = mesh_path / "four_bunny" / "four_bunny.obj";
    std::filesystem::path prefab_path = cmc->GetAssetManager()->GetAssetsDirectory() / mesh_path.filename();
    prefab_path.replace_extension(".gameobject.asset");
    nlohmann::json prefab_json;
    std::ifstream prefab_file(prefab_path);
    prefab_file >> prefab_json;
    prefab_file.close();
    GUID prefab_guid(prefab_json["%data"]["&0"]["Asset::m_guid"].get<std::string>());
    auto prefab_asset =
        dynamic_pointer_cast<GameObjectAsset>(cmc->GetAssetManager()->LoadAssetImmediately(prefab_guid));
    prefab_asset->m_MainObject->AddComponent<SpinningComponent>();
    prefab_asset->m_MainObject->m_name = "Spinning Bunny";
    auto &spinning_transform = prefab_asset->m_MainObject->GetTransformRef();
    spinning_transform.SetPosition(spinning_transform.GetPosition() + glm::vec3(0.0f, 0.2f, 0.0f));
    world->LoadGameObjectAsset(prefab_asset);

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

    auto camera_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    camera_go->m_name = "Main Camera";
    Transform transform{};
    transform.SetPosition({0.0f, 0.2f, -0.7f});
    transform.SetRotationEuler(glm::vec3{1.57, 0.0, 3.1415926});
    transform.SetScale({1.0f, 1.0f, 1.0f});
    camera_go->SetTransform(transform);
    auto camera_comp = camera_go->template AddComponent<CameraComponent>();
    camera_comp->m_camera->set_aspect_ratio(1.0 * opt.resol_x / opt.resol_y);
    auto control_comp = camera_go->template AddComponent<ControlComponent>();
    control_comp->m_camera = camera_comp;
    cmc->GetWorldSystem()->m_active_camera = camera_comp->m_camera;
    cmc->GetWorldSystem()->AddGameObjectToWorld(camera_go);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Create Editor Window");
    Editor::MainWindow main_window;
    auto scene_widget = std::make_shared<Editor::SceneWidget>(Editor::MainWindow::k_scene_widget_name);
    scene_widget->CreateRenderTargetBinding(rsys);
    main_window.AddWidget(scene_widget);
    auto game_widget = std::make_shared<Editor::GameWidget>(Editor::MainWindow::k_game_widget_name);
    game_widget->CreateRenderTargetBinding(rsys);
    main_window.AddWidget(game_widget);

    main_window.m_OnStart.AddDelegate(std::make_unique<FuncDelegate<>>(Start));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");

    bool onQuit = false;
    while (!onQuit) {
        cmc->GetTimeSystem()->NextFrame();

        asset_manager->LoadAssetsInQueue();

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
        world->LoadGameObjectInQueue();

        if (main_window.m_is_playing) {
            world->AddTickEvent();
            event_queue->ProcessEvents();
        }

        rsys->StartFrame();
        auto context = rsys->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer &cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());

        cb.Begin();

        scene_widget->PreRender();
        game_widget->PreRender();

        context.UseImage(
            *std::static_pointer_cast<Engine::Texture>(scene_widget->m_color_texture),
            GraphicsContext::ImageGraphicsAccessType::ShaderRead,
            GraphicsContext::ImageAccessType::ColorAttachmentWrite
        );
        context.UseImage(
            *std::static_pointer_cast<Engine::Texture>(game_widget->m_color_texture),
            GraphicsContext::ImageGraphicsAccessType::ShaderRead,
            GraphicsContext::ImageAccessType::ColorAttachmentWrite
        );
        context.UseImage(
            window->GetColorTexture(),
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.UseImage(
            window->GetDepthTexture(),
            GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.PrepareCommandBuffer();
        gui->PrepareGUI();
        main_window.Render();
        gui->DrawGUI(
            {window->GetColorTexture().GetImage(),
             window->GetColorTexture().GetImageView(),
             vk::AttachmentLoadOp::eLoad,
             vk::AttachmentStoreOp::eStore},
            window->GetExtent(),
            cb
        );

        cb.End();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageBlitComposition(
            window->GetColorTexture().GetImage(), window->GetExtent(), window->GetExtent()
        );
        rsys->GetFrameManager().CompositeToFramebufferAndPresent();
    }
    rsys->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");

    return 0;
}
