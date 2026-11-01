#include <SDL3/SDL.h>
#include <cassert>
#include <stdexcept>
#include <filesystem>
#include <fstream>

#include "Asset/AssetManager/AssetManager.h"
#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Render/RenderSystem/IPresentProvider.h"

#include "cmake_config.h"
#include <iostream>

using namespace Engine;

constexpr uint32_t IMAGE_WIDTH = 512;
constexpr uint32_t IMAGE_HEIGHT = 512;
constexpr uint32_t PIXEL_COUNT = IMAGE_WIDTH * IMAGE_HEIGHT;

// Simple color value to clear with: bright red.
constexpr std::array<uint8_t, 4> CLEAR_COLOR = {200, 30, 30, 255};

void WritePPM(const std::filesystem::path &path, const uint8_t *rgba) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << IMAGE_WIDTH << " " << IMAGE_HEIGHT << "\n255\n";
    for (uint32_t i = 0; i < PIXEL_COUNT; i++) {
        file.put(static_cast<char>(rgba[i * 4 + 0]));
        file.put(static_cast<char>(rgba[i * 4 + 1]));
        file.put(static_cast<char>(rgba[i * 4 + 2]));
    }
}

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Headless Offscreen Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);
    assert(cmc->GetWindow() == nullptr && "Headless test should not create a window.");

    auto rsys = cmc->GetRenderSystem();

    // Prepare attachments
    RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = IMAGE_WIDTH,
        .height = IMAGE_HEIGHT,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    auto color = RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Headless Color Target"
    );
    auto readback_buffer = Rhi::DeviceBuffer::CreateUnique(
        rsys->GetAllocatorState(),
        Engine::Rhi::BufferType{Engine::Rhi::BufferTypeBits::ReadbackFromDevice},
        color->CalculateStagingBufferSizeNoMipmap()
    );
    auto *readback_raw = readback_buffer.get();

    RenderGraphBuilder rgb{*rsys};
    auto c = rgb.ImportExternalResource(*color);
    auto rb = rgb.ImportExternalResource(*readback_buffer);
    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Clear")
            .UseImage(c, Rhi::MemoryAccessTypeImageBits::TransferWrite)
            .SetAffinity(RenderGraphPassAffinity::Transfer)
            .SetPassFunction([c](CommandBuffer &cb, const RenderGraph &rg) {
                auto rt = rg.GetInternalTextureResource(c);
                vk::ClearColorValue clear_color{
                    CLEAR_COLOR[0] / 255.0f, CLEAR_COLOR[1] / 255.0f, CLEAR_COLOR[2] / 255.0f, CLEAR_COLOR[3] / 255.0f
                };
                vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
                cb.GetCommandBuffer().clearColorImage(
                    Engine::Rhi::ToVulkanImage(rt->GetImage()), vk::ImageLayout::eTransferDstOptimal, clear_color, range
                );
            })
            .Get()
    );
    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Readback")
            .UseImage(c, Rhi::MemoryAccessTypeImageBits::TransferRead)
            .UseBuffer(rb, {Rhi::MemoryAccessTypeBufferBits::TransferWrite})
            .SetAffinity(RenderGraphPassAffinity::Transfer)
            .SetPassFunction([c, readback_raw](CommandBuffer &cb, const RenderGraph &rg) {
                auto rt = rg.GetInternalTextureResource(c);
                std::array image_copies = {vk::BufferImageCopy{
                    0,
                    0,
                    0,
                    vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                    vk::Offset3D{0, 0, 0},
                    vk::Extent3D{
                        rt->GetTextureDescription().width,
                        rt->GetTextureDescription().height,
                        rt->GetTextureDescription().depth
                    }
                }};
                cb.GetCommandBuffer().copyImageToBuffer(
                    Engine::Rhi::ToVulkanImage(rt->GetImage()), vk::ImageLayout::eTransferSrcOptimal, Rhi::ToVulkanBuffer(readback_raw->GetBuffer()), image_copies
                );
            })
            .Get()
    );
    auto rg = rgb.BuildRenderGraph();

    // Execute multiple frames headlessly. Running ≥4 frames exercises the full
    // frames-in-flight sync chain (FRAMES_IN_FLIGHT = 3): without the
    // frame-completion submit (FrameManager::SubmitFrame) signaling
    // timeline@4 + the command-executed fence unconditionally, the 2nd frame's
    // timeline wait and the 4th frame's fence wait would deadlock. The last
    // frame's content is read back below.
    for (uint32_t frame = 0; frame < 4; frame++) {
        if (rsys->StartFrame() == std::numeric_limits<uint32_t>::max()) {
            // Headless acquire never fails, but keep the same guard for
            // symmetry with windowed loops.
            continue;
        }
        rsys->GetFrameManager().BeginMainCommandBuffer();

        // A mark inside a pass is refused. The rule exists because the WebGPU
        // backend has no transitions: a barrier recorded mid-pass has nothing to
        // move, so the seam states the rule and the Vulkan encoder enforces it
        // rather than doing something undefined. This is the only place the guard
        // is exercised, and it runs once.
        if (frame == 0) {
            Rhi::RenderPassDescription guard_pass{};
            guard_pass.area = Rhi::Rect2D{Rhi::Offset2D{0, 0}, Rhi::Extent2D{IMAGE_WIDTH, IMAGE_HEIGHT}};
            Rhi::ColorAttachmentDescription guard_attachment{};
            guard_attachment.view = color->GetImageView();
            guard_attachment.load = Rhi::LoadOperation::DontCare;
            guard_attachment.store = Rhi::StoreOperation::Store;
            guard_pass.color_attachments.push_back(guard_attachment);

            auto &encoder = rsys->GetFrameManager().GetMainEncoder();
            encoder.BeginRenderPass(guard_pass);
            bool refused = false;
            try {
                encoder.Barrier();
            } catch (const std::logic_error &) {
                refused = true;
            }
            encoder.EndRenderPass();
            if (!refused) {
                std::cerr << "Headless offscreen test FAILED: a mark inside a render pass was accepted." << std::endl;
                return 1;
            }
        }

        rg->RecordIntoMainCommandBuffer(*rsys);
        rsys->CompleteFrame(*color, Rhi::MemoryAccessTypeImageBits::TransferRead);
    }
    rsys->WaitForIdle();

    // Read back and verify.
    auto *pixels = reinterpret_cast<const uint8_t *>(readback_buffer->GetVMAddress());
    bool pass = true;
    for (uint32_t i = 0; i < 16; i++) {
        const auto idx = i * 4;
        if (pixels[idx + 0] != CLEAR_COLOR[0] || pixels[idx + 1] != CLEAR_COLOR[1]
            || pixels[idx + 2] != CLEAR_COLOR[2]) {
            std::cerr << "Mismatch at byte " << idx << ": got (" << (int)pixels[idx + 0] << ", " << (int)pixels[idx + 1]
                      << ", " << (int)pixels[idx + 2] << ")" << std::endl;
            pass = false;
            break;
        }
    }

    std::filesystem::path output_path = std::filesystem::temp_directory_path() / "headless_offscreen.ppm";
    WritePPM(output_path, pixels);
    std::cout << "Wrote " << output_path << " (" << std::filesystem::file_size(output_path) << " bytes)." << std::endl;

    if (!pass) {
        std::cerr << "Headless offscreen test FAILED: pixel content does not match clear color." << std::endl;
        return 1;
    }
    std::cout << "Headless offscreen test PASSED." << std::endl;
    return 0;
}



