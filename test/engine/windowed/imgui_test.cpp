#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <chrono>
#include <fstream>

#include "Core/Functional/SDLWindow.h"
#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/UserInterface/GUISystem.h"

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
    gsys->CreateBackend(*rsys, Engine::Rhi::ImageFormat::R8G8B8A8UNorm);

    RenderGraphBuilder rgb{*rsys};
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
    auto c = rgb.RequestRenderTargetTexture(desc, {});

    // Read the composed frame back so the test can assert that the UI drew.
    auto readback_buffer = Rhi::DeviceBuffer::CreateUnique(
        rsys->GetAllocatorState(),
        Rhi::BufferType{Rhi::BufferTypeBits::ReadbackFromDevice},
        1920u * 1080u * 4u
    );
    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .AppendColorAttachment(
                {c, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
            )
            .SetPassFunction([&](CommandBuffer &cb, const RenderGraph &rg) -> void {
                auto color = rg.GetInternalTextureResource(c);
                gsys->DrawGUI(
                    AttachmentUtils::AttachmentDescription{
                        color,
                        Rhi::TextureSubresourceRange::GetSingleRange(),
                        AttachmentUtils::LoadOperation::Clear,
                        AttachmentUtils::StoreOperation::Store,
                    },
                    Rhi::Extent2D{color->GetTextureDescription().width, color->GetTextureDescription().height},
                    cb
                );
            })
            .Get()
    );
    {
        const auto rb = rgb.ImportExternalResource(*readback_buffer);
        rgb.AddPass(
            RenderGraphPassBuilder{*rsys}
                .SetName("Frame Readback")
                .UseImage(c, Rhi::MemoryAccessTypeImageBits::TransferRead)
                .UseBuffer(rb, {Rhi::MemoryAccessTypeBufferBits::TransferWrite})
                .SetAffinity(RenderGraphPassAffinity::Transfer)
                .SetPassFunction([c, readback = readback_buffer.get()](CommandBuffer &cb, const RenderGraph &graph) {
                    auto rt = graph.GetInternalTextureResource(c);
                    cb.GetEncoder().CopyImageToBuffer(
                        rt->GetImage(),
                        readback->GetBuffer(),
                        Rhi::Extent3D{rt->GetTextureDescription().width, rt->GetTextureDescription().height, 1},
                        Rhi::ImageAspect::Color
                    );
                })
                .Get()
        );
    }

    auto rg = rgb.BuildRenderGraph();

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
        if (index == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after retry — skip this frame.
            continue;
        }
        assert(index < rsys->GetPresentProvider().GetImageCount());
        rsys->GetFrameManager().BeginMainCommandBuffer();
        rg->RecordIntoMainCommandBuffer(*rsys);
        auto color = rg->GetInternalTextureResource(c);
        rsys->CompleteFrame(*color, Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite);

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    // If the UI pipeline drew nothing the frame is uniformly black.
    {
        const auto *pixels = reinterpret_cast<const uint8_t *>(readback_buffer->GetVMAddress());
        constexpr size_t PIXEL_COUNT = 1920u * 1080u;
        size_t non_black = 0;
        for (size_t i = 0; i < PIXEL_COUNT; i++) {
            if (pixels[i * 4 + 0] != 0 || pixels[i * 4 + 1] != 0 || pixels[i * 4 + 2] != 0) {
                non_black++;
            }
        }
        if (non_black == 0) {
            std::cerr << "ImGui test FAILED: the frame is entirely black; the UI did not draw." << std::endl;
            return 1;
        }
        SDL_LogInfo(0, "ImGui test: %zu of %zu pixels are not black.", non_black, PIXEL_COUNT);
    }

    return 0;
}






