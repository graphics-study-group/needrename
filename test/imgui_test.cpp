#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/RenderSystem.h"
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

    RenderTargetSetup rts{rsys};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });

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
        cb.BeginRendering(rts, extent, index);
        gsys->DrawGUI(cb);
        cb.EndRendering();
        cb.End();
        cb.Submit();

        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    return 0;
}
