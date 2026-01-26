#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Core/Functional/SDLWindow.h"
#include "UserInterface/GUISystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"

using namespace Engine;
namespace sch = std::chrono;

auto BuildRenderGraph(RenderSystem & rsys, GUISystem & gsys, RenderTargetTexture & color) {
    RenderGraphBuilder rgb{rsys};
    rgb.UseImage(color, MemoryAccessTypeImageBits::ColorAttachmentDefault);
    rgb.RecordRasterizerPassWithoutRT(
        [&] (GraphicsCommandBuffer & gcb) -> void {
            gsys.DrawGUI(
                AttachmentUtils::AttachmentDescription{
                    &color, nullptr,
                    AttachmentUtils::LoadOperation::Clear,
                    AttachmentUtils::StoreOperation::Store,
                },
                vk::Extent2D{
                    color.GetTextureDescription().width, 
                    color.GetTextureDescription().height
                },
                gcb
            );
        }
    );
    return rgb.BuildRenderGraph();
}

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
    gsys->CreateVulkanBackend(*rsys, ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm));

    Engine::RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    auto color = RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color attachment");
    auto rg = BuildRenderGraph(*rsys, *gsys, *color);

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
        ImGui::SetCurrentContext(gsys->GetCurrentContext());
        ImGui::ShowDemoWindow();

        auto index = rsys->StartFrame();
        auto context = rsys->GetFrameManager().GetGraphicsContext();

        assert(index < 3);
        rg->Execute();
        rsys->CompleteFrame(*color, color->GetTextureDescription().width, color->GetTextureDescription().height);

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    return 0;
}
