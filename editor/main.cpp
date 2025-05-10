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
#include <GUI/GUISystem.h>
#include <imgui_impl_vulkan.h>
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

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
    auto gui = std::make_shared<GUISystem>(rsys);
    gui->Create(cmc->GetWindow()->GetWindow());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    Engine::AllocatedImage2D color{rsys}, depth{rsys};
    color.Create(1920, 1080, Engine::ImageUtils::ImageType::ColorGeneral, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
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

    vk::SamplerCreateInfo sci{};
    sci.magFilter = sci.minFilter = vk::Filter::eNearest;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = vk::SamplerAddressMode::eRepeat;
    vk::Sampler sampler = rsys->getDevice().createSampler(sci);
    ImTextureID color_att_id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(sampler, color_att.image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    Engine::AllocatedImage2D color_editor{rsys}, depth_editor{rsys};
    color_editor.Create(1920, 1080, Engine::ImageUtils::ImageType::ColorAttachment, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
    depth_editor.Create(1920, 1080, Engine::ImageUtils::ImageType::DepthImage, Engine::ImageUtils::ImageFormat::D32SFLOAT, 1);

    Engine::AttachmentUtils::AttachmentDescription color_editor_att, depth_editor_att;
    color_editor_att.image = color_editor.GetImage();
    color_editor_att.image_view = color_editor.GetImageView();
    color_editor_att.load_op = vk::AttachmentLoadOp::eClear;
    color_editor_att.store_op = vk::AttachmentStoreOp::eStore;

    depth_editor_att.image = depth_editor.GetImage();
    depth_editor_att.image_view = depth_editor.GetImageView();
    depth_editor_att.load_op = vk::AttachmentLoadOp::eClear;
    depth_editor_att.store_op = vk::AttachmentStoreOp::eDontCare;

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
            gui->ProcessEvent(&event);
            if (gui->WantCaptureMouse() && SDL_EVENT_MOUSE_MOTION <= event.type && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) // 0x600+
                continue;
            if (gui->WantCaptureKeyboard() && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
                continue;
            cmc->GetInputSystem()->ProcessEvent(&event);
        }

        cmc->GetInputSystem()->Update(dt);
        world->Tick(dt);

        ImGui_ImplSDL3_NewFrame();

        // Draw
        rsys->StartFrame();
        RenderCommandBuffer &cb = rsys->GetCurrentCommandBuffer();

        cb.Begin();
        {
            cb.BeginRendering(color_att, depth_att, color.GetExtent());
            rsys->DrawMeshes();
            // TODO: Event Process ????
            // ImGui_ImplVulkan_NewFrame();
            // ImGui::NewFrame();
            // gui->DrawGUI(cb);
            cb.EndRendering();
        }

        cb.InsertAttachmentBarrier(RenderCommandBuffer::AttachmentBarrierType::ColorAttachmentRAW, color_att.image);

        {
            vk::Extent2D extent2{rsys->GetSwapchain().GetExtent()};
            cb.BeginRendering(color_editor_att, depth_editor_att, extent2);
            ImGui_ImplVulkan_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({400, 10});
            ImGui::SetNextWindowSize(ImVec2{1300, 760});
            ImGui::Begin("Game");
            ImGui::Image(color_att_id, ImVec2(1280, 720));
            ImGui::End();
            gui->DrawGUI(cb);
            cb.EndRendering();
        }

        cb.End();
        cb.Submit();

        rsys->GetFrameManager().StageCopyComposition(color_editor.GetImage());
        rsys->CompleteFrame();
    }
    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");

    return 0;
}
