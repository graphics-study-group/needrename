#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>

#include "Render/FullRenderSystem.h"
#include <Asset/AssetManager/AssetManager.h>
#include <Core/Delegate/FuncDelegate.h>
#include <Framework/world/WorldSystem.h>
#include <Functional/EventQueue.h>
#include <Functional/SDLWindow.h>
#include <Functional/Time.h>
#include <GUI/GUISystem.h>
#include <Input/Input.h>
#include <MainClass.h>
#include <SDL3/SDL.h>
#include <cmake_config.h>

#include <Editor/Widget/GameWidget.h>
#include <Editor/Widget/SceneWidget.h>
#include <Editor/Window/MainWindow.h>

using namespace Engine;

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

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Create Editor Window");
    Editor::MainWindow main_window;
    auto scene_widget = std::make_shared<Editor::SceneWidget>(Editor::MainWindow::k_scene_widget_name);
    scene_widget->CreateRenderTargets(rsys);
    main_window.AddWidget(scene_widget);
    auto game_widget = std::make_shared<Editor::GameWidget>(Editor::MainWindow::k_game_widget_name);
    game_widget->CreateRenderTargets(rsys);
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
            {&window->GetColorTexture(),
             nullptr,
             Engine::AttachmentUtils::LoadOperation::Load,
             Engine::AttachmentUtils::StoreOperation::Store},
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
