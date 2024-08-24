#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/Pipeline.h"
#include "Render/Pipeline/Framebuffers.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/PremadeRenderPass/SingleRenderPass.h"

using namespace Engine;
namespace sch = std::chrono;

Engine::MainClass * cmc;

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) throw std::runtime_error("failed to open file!");

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

int main(int, char **)
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE);
    cmc->Initialize(&opt);

    auto system = cmc->GetRenderSystem();
    PipelineLayout pl{system};
    pl.CreatePipelineLayout({}, {});

    SingleRenderPass rp{system};
    rp.CreateFramebuffers();

    Pipeline p{system};
    ShaderModule fragModule {system};
    ShaderModule vertModule {system};
    fragModule.CreateShaderModule(readFile("shader/debug_fragment_color.frag.spv"));
    vertModule.CreateShaderModule(readFile("shader/debug_vertex_color.vert.spv"));

    p.CreatePipeline(rp.GetSubpass(0), pl, {
        fragModule.GetStageCreateInfo(vk::ShaderStageFlagBits::eFragment),
        vertModule.GetStageCreateInfo(vk::ShaderStageFlagBits::eVertex)
        });
    
    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 60;
    
    system->UpdateSwapchain();
    rp.CreateFramebuffers();

    uint64_t start_timer = SDL_GetPerformanceCounter();
    while(total_test_frame--) {
        
        auto frame_start_timer = sch::high_resolution_clock::now();

        vk::Fence fence = system->getSynchronization().GetCommandBufferFence(in_flight_frame_id);
        vk::Result waitFenceResult = system->getDevice().waitForFences({fence}, vk::True, 0x7FFFFFFF);
        if (waitFenceResult == vk::Result::eTimeout) {
            SDL_LogError(0, "Timed out waiting for fence.");
            return -1;
        }
        CommandBuffer & cb = system->GetGraphicsCommandBufferWaitAndReset(in_flight_frame_id, 0x7fffffff);
        system->getDevice().resetFences({fence});

        auto fence_end_timer = sch::high_resolution_clock::now();

        uint32_t index = system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);
        assert(index < 3);

        auto image_end_timer = sch::high_resolution_clock::now();
    
        cb.Begin();
        cb.BeginRenderPass(rp, system->getSwapchainInfo().extent, index, {{{0.0f, 0.0f, 0.0f, 1.0f}}});

        auto bind_pipeline_begin = sch::high_resolution_clock::now();

        cb.BindPipelineProgram(p);
        vk::Rect2D scissor{{0, 0}, system->getSwapchainInfo().extent};
        cb.SetupViewport(system->getSwapchainInfo().extent.width, system->getSwapchainInfo().extent.height, scissor);
        cb.Draw();
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
