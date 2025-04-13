#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/ImageUtils.h"
#include "Render/AttachmentUtils.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/MaterialRegistry.h"
#include "GUI/GUISystem.h"

using namespace Engine;
namespace sch = std::chrono;

int main(int argc, char ** argv)
{
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
    // rsys->EnableDepthTesting();
    auto gsys = cmc->GetGUISystem();

    Engine::AllocatedImage2D color{rsys}, depth{rsys};
    color.Create(1920, 1080, Engine::ImageUtils::ImageType::ColorAttachment, Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB, 1);
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

    bool quited = false;
    while(max_frame_count--) {
        SDL_Event event;
        while(SDL_PollEvent(&event) != 0) {
            switch(event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
            gsys->ProcessEvent(&event);
        }
        
        gsys->PrepareGUI();
        ImGui::ShowDemoWindow();

        auto index = rsys->StartFrame();
        RenderCommandBuffer & cb = rsys->GetCurrentCommandBuffer();

        assert(index < 3);
    
        cb.Begin();
        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        cb.BeginRendering(color_att, depth_att, extent, index);
        gsys->DrawGUI(cb);
        cb.EndRendering();
        cb.End();
        cb.Submit();
        rsys->GetFrameManager().CopyToFramebuffer(color.GetImage());
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    return 0;
}
