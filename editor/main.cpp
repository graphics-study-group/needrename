#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <fstream>

#include <cmake_config.h>
#include <MainClass.h>
#include <Functional/SDLWindow.h>
#include <Render/RenderSystem.h>
#include <Render/AttachmentUtils.h>
#include <Render/Memory/Image2DTexture.h>
#include <Render/Memory/Buffer.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/RenderSystem/Swapchain.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Framework/world/WorldSystem.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Input/Input.h>

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

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    Engine::AllocatedImage2D color{rsys}, depth{rsys};
    color.Create(1920, 1080, Engine::ImageUtils::ImageType::ColorAttachment, Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB, 1);
    depth.Create(1920, 1080, Engine::ImageUtils::ImageType::DepthImage, Engine::ImageUtils::ImageFormat::D32SFLOAT, 1);

    Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
    color_att.image = color.GetImage();
    color_att.image_view = color.GetImageView();
    color_att.load_op = vk::AttachmentLoadOp::eClear;
    color_att.store_op = vk::AttachmentStoreOp::eStore;

    depth_att.image = depth.GetImage();
    depth_att.image_view = depth.GetImageView();
    depth_att.load_op = vk::AttachmentLoadOp::eClear;
    depth_att.store_op = vk::AttachmentStoreOp::eDontCare;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");

    bool onQuit = false;
    Uint64 FPS_TIMER = 0;
    while (!onQuit)
    {
        Uint64 current_time = SDL_GetTicksNS();
        float dt = (current_time - FPS_TIMER) * 1e-9f;
        FPS_TIMER = current_time;

        asset_manager->LoadAssetsInQueue();

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                onQuit = true;
                break;
            }
            // this->gui->ProcessEvent(&event);
            // if (this->gui->WantCaptureMouse() && SDL_EVENT_MOUSE_MOTION <= event.type && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) // 0x600+
            //     continue;
            // if (this->gui->WantCaptureKeyboard() && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
            //     continue;
            cmc->GetInputSystem()->ProcessEvent(&event);
        }

        cmc->GetInputSystem()->Update(dt);
        world->Tick(dt);

        // Draw
        auto index = rsys->StartFrame();
        RenderCommandBuffer &cb = rsys->GetCurrentCommandBuffer();

        cb.Begin();
        vk::Extent2D extent{rsys->GetSwapchain().GetExtent()};
        cb.BeginRendering(color_att, depth_att, extent, index);
        rsys->DrawMeshes();
        cb.EndRendering();
        cb.End();
        cb.Submit();
        rsys->GetFrameManager().CopyToFramebuffer(color.GetImage());
        rsys->CompleteFrame();
    }
    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");

    return 0;
}
