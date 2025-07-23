#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <fstream>

#include <cmake_config.h>
#include <MainClass.h>
#include <Core/Delegate/FuncDelegate.h>
#include <Functional/SDLWindow.h>
#include <Functional/Time.h>
#include <Functional/EventQueue.h>
#include <Render/RenderSystem.h>
#include <Render/AttachmentUtils.h>
#include <Render/Memory/Buffer.h>
#include <Render/Memory/Texture.h>
#include <Render/Memory/SampledTexture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/RenderSystem/Swapchain.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Framework/world/WorldSystem.h>
#include <Framework/component/Component.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Scene/GameObjectAsset.h>
#include <Input/Input.h>
#include <GUI/GUISystem.h>
#include <SDL3/SDL.h>

#include <Editor/Window/MainWindow.h>
#include <Editor/Widget/GameWidget.h>
#include <Editor/Widget/SceneWidget.h>

using namespace Engine;

class SpinningComponent : public Component
{
public:
    SpinningComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {}

    float m_speed = 30.0f;

    void Init() override
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SpinningComponent Init");
    }

    void Tick() override
    {
        float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
        auto go = m_parentGameObject.lock();
        if (go)
        {
            auto &transform = go->GetTransformRef();
            transform.SetRotation(transform.GetRotation() * glm::angleAxis(glm::radians(m_speed * dt), glm::vec3(0.0f, 1.0f, 0.0f)));
        }
    }
};

void Start()
{
    auto cmc = MainClass::GetInstance();
    auto world = cmc->GetWorldSystem();
    auto event_queue = cmc->GetEventQueue();
    event_queue->Clear();
    world->AddInitEvent();
}

int main()
{
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
    auto prefab_asset = dynamic_pointer_cast<GameObjectAsset>(cmc->GetAssetManager()->LoadAssetImmediately(prefab_guid));
    prefab_asset->m_MainObject->AddComponent<SpinningComponent>();
    prefab_asset->m_MainObject->m_name = "Spinning Bunny";
    auto &spinning_transform = prefab_asset->m_MainObject->GetTransformRef();
    spinning_transform.SetPosition(spinning_transform.GetPosition() + glm::vec3(0.0f, 0.2f, 0.0f));
    world->LoadGameObjectAsset(prefab_asset);

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
    while (!onQuit)
    {
        cmc->GetTimeSystem()->NextFrame();

        asset_manager->LoadAssetsInQueue();

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                onQuit = true;
                break;
            }
            gui->ProcessEvent(&event);
            if (gui->WantCaptureMouse() && SDL_EVENT_MOUSE_MOTION <= event.type && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) // 0x600+
                continue;
            if (gui->WantCaptureKeyboard() && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
                continue;
            cmc->GetInputSystem()->ProcessEvent(&event);
        }

        cmc->GetInputSystem()->Update();
        world->LoadGameObjectInQueue();

        if (main_window.m_is_playing)
        {
            world->AddTickEvent();
            event_queue->ProcessEvents();
        }

        rsys->StartFrame();
        auto context = rsys->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer &cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());

        cb.Begin();

        scene_widget->PreRender();
        game_widget->PreRender();

        context.UseImage(*std::static_pointer_cast<Engine::Texture>(scene_widget->m_color_texture), GraphicsContext::ImageGraphicsAccessType::ShaderRead, GraphicsContext::ImageAccessType::ColorAttachmentWrite);
        context.UseImage(*std::static_pointer_cast<Engine::Texture>(game_widget->m_color_texture), GraphicsContext::ImageGraphicsAccessType::ShaderRead, GraphicsContext::ImageAccessType::ColorAttachmentWrite);
        context.UseImage(window->GetColorTexture(), GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite, GraphicsContext::ImageAccessType::None);
        context.UseImage(window->GetDepthTexture(), GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite, GraphicsContext::ImageAccessType::None);
        context.PrepareCommandBuffer();
        gui->PrepareGUI();
        main_window.Render();
        gui->DrawGUI({window->GetColorTexture().GetImage(),
                      window->GetColorTexture().GetImageView(),
                      vk::AttachmentLoadOp::eLoad,
                      vk::AttachmentStoreOp::eStore},
                     window->GetExtent(), cb);

        cb.End();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageBlitComposition(window->GetColorTexture().GetImage(), window->GetExtent(), window->GetExtent());
        rsys->GetFrameManager().CompositeToFramebufferAndPresent();
    }
    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");

    return 0;
}
