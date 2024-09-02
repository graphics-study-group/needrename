#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Render/Material/TestMaterial.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/Framebuffers.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/PremadePipeline/DefaultPipeline.h"
#include "Render/Pipeline/PremadePipeline/SingleRenderPass.h"
#include "Render/Renderer/HomogeneousMesh.h"

using namespace Engine;
namespace sch = std::chrono;

Engine::MainClass * cmc;

class TestHomoMesh : public HomogeneousMesh {
public:
    TestHomoMesh(std::weak_ptr<RenderSystem> system) : HomogeneousMesh(system) {
        this->m_positions = {{0.0f, -0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}};
        this->m_attributes = {
            {.color = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}, 
            {.color = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}, 
            {.color = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}
        };
        this->m_indices = {0, 1, 2};
    }
};

int main(int, char **)
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE);
    cmc->Initialize(&opt);

    auto system = cmc->GetRenderSystem();

    SingleRenderPass rp{system};
    rp.CreateFramebuffersFromSwapchain();
    rp.SetClearValues({{{0.0f, 0.0f, 0.0f, 1.0f}}});

    TestMaterial material{system, rp};

    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 60;
    
    system->UpdateSwapchain();
    rp.CreateFramebuffersFromSwapchain();

    TestHomoMesh mesh{system};
    mesh.Prepare();
    
    uint64_t start_timer = SDL_GetPerformanceCounter();
    while(total_test_frame--) {
        
        auto frame_start_timer = sch::high_resolution_clock::now();

        system->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = system->GetGraphicsCommandBuffer(in_flight_frame_id);

        auto fence_end_timer = sch::high_resolution_clock::now();

        uint32_t index = system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);
        assert(index < 3);

        auto image_end_timer = sch::high_resolution_clock::now();
    
        cb.Begin();
        if (mesh.NeedCommitment()) {
            cb.CommitVertexBuffer(mesh);
        }
        vk::Extent2D extent {system->GetSwapchain().GetExtent()};
        cb.BeginRenderPass(rp, extent, index);

        auto bind_pipeline_begin = sch::high_resolution_clock::now();

        cb.BindMaterial(material, 0);
        vk::Rect2D scissor{{0, 0}, system->GetSwapchain().GetExtent()};
        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.DrawMesh(mesh);
        cb.End();

        cb.SubmitToQueue(system->getQueueInfo().graphicsQueue, system->getSynchronization());

        auto submit_end_timer = sch::high_resolution_clock::now();

        system->Present(index, in_flight_frame_id);

        auto submission_end_timer = sch::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> waiting, image, begining, record, presenting, total;
        waiting = fence_end_timer - frame_start_timer;
        image = image_end_timer - fence_end_timer;
        begining = bind_pipeline_begin - image_end_timer;
        record = submit_end_timer - bind_pipeline_begin;
        presenting = submission_end_timer - submit_end_timer;
        total = submission_end_timer - frame_start_timer;

        SDL_LogVerbose(0, "Waiting for fence for %lf milliseconds.", waiting.count());
        SDL_LogVerbose(0, "Waiting for next image for %lf milliseconds.", image.count());
        SDL_LogVerbose(0, "Begining recording command buffer for %lf milliseconds.", begining.count());
        SDL_LogVerbose(0, "Recording for %lf milliseconds.", record.count());
        SDL_LogVerbose(0, "Presenting for %lf milliseconds.", presenting.count());
        SDL_LogVerbose(0, "Total: %lf milliseconds, or %lf fps for frame %u.", 
            total.count(),
            1000.0 / total.count(),
            in_flight_frame_id
            );

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
    }
    uint64_t end_timer = SDL_GetPerformanceCounter();
    uint64_t duration = end_timer - start_timer;
    double duration_time = 1.0 * duration / SDL_GetPerformanceFrequency();
    SDL_LogInfo(0, "Took %lf seconds for 200 frames (avg. %lf fps).", duration_time, 200.0 / duration_time);
    system->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
