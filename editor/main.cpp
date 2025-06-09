#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <fstream>

#include <cmake_config.h>
#include <MainClass.h>
#include <Functional/SDLWindow.h>
#include <Render/RenderSystem.h>
#include <Render/AttachmentUtils.h>
#include <Render/Memory/Buffer.h>
#include <Render/Memory/Texture.h>
#include <Render/Pipeline/CommandBuffer/GraphicsContext.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/RenderSystem/Swapchain.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Framework/world/WorldSystem.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Input/Input.h>
#include <GUI/GUISystem.h>
#include <SDL3/SDL.h>

#include <Editor/Window/MainWindow.h>
#include <Editor/Widget/GameWidget.h>

using namespace Engine;

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

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Create Editor Window");
    Editor::MainWindow main_window;
    main_window.AddWidget(std::make_shared<Editor::Widget>("Test Widget"));
    main_window.AddWidget(std::make_shared<Editor::Widget>("Test Widget 2"));
    auto game_widget = std::make_shared<Editor::GameWidget>("Game");
    game_widget->CreateRenderTargetBinding(rsys);
    main_window.AddWidget(game_widget);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");

    bool onQuit = false;
    Uint64 FPS_TIMER = 0;
    unsigned int frame_count = 0;
    while (!onQuit)
    {
        Uint64 current_time = SDL_GetTicksNS();
        float dt = (current_time - FPS_TIMER) * 1e-9f;
        FPS_TIMER = current_time;
        frame_count++;

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

        cmc->GetInputSystem()->Update(dt);
        world->Tick(dt);

        auto index = rsys->StartFrame();
        auto context = rsys->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer & cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());

        cb.Begin();
        context.UseImage(window->GetColorTexture(), GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite, GraphicsContext::ImageAccessType::None);
        context.UseImage(window->GetDepthTexture(), GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite, GraphicsContext::ImageAccessType::None);
        context.PrepareCommandBuffer();

        gui->PrepareGUI();
        game_widget->PreRender(cb);
        main_window.Render();

        cb.BeginRendering(window->GetRenderTargetBinding(), window->GetExtent(), "Editor Pass");
        rsys->DrawMeshes();
        gui->DrawGUI(cb);
        cb.EndRendering();
        cb.End();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageCopyComposition(window->GetColorTexture().GetImage());
        rsys->GetFrameManager().CompositeToFramebufferAndPresent();
    }
    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");

    return 0;
}
