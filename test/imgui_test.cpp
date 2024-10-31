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

Engine::MainClass * cmc;

int main(int, char **)
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE);
    cmc->Initialize(&opt);

    auto rsys = cmc->GetRenderSystem();
    rsys->EnableDepthTesting();
    auto gsys = cmc->GetGUISystem();

    RenderTargetSetup rts{rsys};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });

    uint32_t in_flight_frame_id = 0;
    bool quited = false;
    while(!quited) {
        SDL_Event event;
        while(SDL_PollEvent(&event) != 0) {
            switch(event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
        }
        gsys->ProcessEvent(&event);
        gsys->PrepareGUI();
        ImGui::ShowDemoWindow();

        rsys->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = rsys->GetGraphicsCommandBuffer(in_flight_frame_id);
        uint32_t index = rsys->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);

        assert(index < 3);
    
        cb.Begin();
        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        cb.BeginRendering(rts, extent, index);
        gsys->DrawGUI(cb);
        cb.EndRendering();
        cb.End();
        cb.Submit();

        rsys->Present(index, in_flight_frame_id);

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
