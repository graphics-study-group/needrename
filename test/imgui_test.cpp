#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Functional/SDLWindow.h"
#include "GUI/GUISystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"

using namespace Engine;
namespace sch = std::chrono;

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto rsys = cmc->GetRenderSystem();
    auto gsys = cmc->GetGUISystem();
    gsys->CreateVulkanBackend(ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB));

    Engine::Texture color{*rsys}, depth{*rsys};
    Engine::Texture::TextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .format = Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB,
        .type = Engine::ImageUtils::ImageType::ColorAttachment,
        .mipmap_levels = 1,
        .array_layers = 1,
        .is_cube_map = false
    };
    color.CreateTexture(desc, "Color Attachment");
    desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
    desc.type = Engine::ImageUtils::ImageType::DepthImage;
    depth.CreateTexture(desc, "Depth Attachment");

    Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
    color_att.image = color.GetImage();
    color_att.image_view = color.GetImageView();
    color_att.load_op = vk::AttachmentLoadOp::eClear;
    color_att.store_op = vk::AttachmentStoreOp::eStore;

    depth_att.image = depth.GetImage();
    depth_att.image_view = depth.GetImageView();
    depth_att.load_op = vk::AttachmentLoadOp::eClear;
    depth_att.store_op = vk::AttachmentStoreOp::eDontCare;

    bool quited = false;
    while (max_frame_count--) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
            gsys->ProcessEvent(&event);
        }

        gsys->PrepareGUI();
        ImGui::ShowDemoWindow();

        auto index = rsys->StartFrame();
        auto context = rsys->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer &cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());

        assert(index < 3);

        cb.Begin();
        context.UseImage(
            color,
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.UseImage(
            depth,
            GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.PrepareCommandBuffer();
        vk::Extent2D extent{rsys->GetSwapchain().GetExtent()};
        gsys->DrawGUI(color_att, extent, cb);
        cb.End();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageCopyComposition(color.GetImage());
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    return 0;
}
