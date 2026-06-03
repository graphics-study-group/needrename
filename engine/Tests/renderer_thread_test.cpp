#include "Core/Functional/SDLWindow.h"
#include "Render/FullRenderSystem.h"
#include "Render/RenderThread/RenderServiceQueue.h"
#include "Render/RenderThread/RenderThread.h"
#include "Render/RenderThread/RenderThreadState.h"
#include "Render/RenderThread/Tasks/RenderTasks.h"
#include <SDL3/SDL.h>
#include <iostream>

using namespace Engine;

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return -1;

    constexpr auto sdl_window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    auto window = std::make_shared<SDLWindow>("TEST", 400, 300, sdl_window_flags);

    auto rgb = std::make_unique<RenderGraphBuilder2>();

    auto rttd = RenderTargetTexture::RenderTargetTextureDesc{
        .dimensions = 2,
        .width = 400,
        .height = 300,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
    };
    auto mbuffer = rgb->RequestRenderTargetTexture(rttd, {}, "Main buffer");
    rgb->AddPass(
        RenderGraphPassBuilder{}
            .SetName("Main pass")
            .AppendColorAttachment(
                {mbuffer,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store,
                 {AttachmentUtils::ColorClearValue{1.0f, 0.0f, 0.0f, 1.0f}}}
            )
            .SetRasterizerPassFunction([](GraphicsCommandBuffer &, const RenderGraph2 &) {})
            .WrapRenderPass()
            .Get()
    );

    auto thd = RenderThread{window};
    auto &s = *thd.BlockAndAcquireThreadState().render_system;
    auto p = s.GetDeviceInterface().GetPhysicalDevice().getProperties();
    std::cout << p.deviceName << "\n";

    thd.Resume();
    thd.GetServiceQueue().push(std::make_unique<RenderTasks::RenderTaskBuildActiveRenderGraph>(std::move(rgb)));
    thd.GetServiceQueue().push(std::make_unique<RenderTasks::RenderTaskRenderOneFrameWithActiveGraph>(mbuffer));

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1ms);

    return 0;
}
