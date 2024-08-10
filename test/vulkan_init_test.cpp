#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/Pipeline.h"
#include "Render/Pipeline/Framebuffers.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/PremadeRenderPass/SingleRenderPass.h"

using namespace Engine;

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

    StartupOptions opt{.resol_x = 800, .resol_y = 600, .title = "Vulkan Test"};

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
    
    while(1) {
        vk::Fence fence = system->getSynchronization().GetCommandBufferFence(0);
        vk::Result waitFenceResult = system->getDevice().waitForFences({fence}, vk::True, 0x7FFFFFFF);
        if (waitFenceResult == vk::Result::eTimeout) {
            SDL_LogError(0, "Timed out waiting for fence.");
            return -1;
        }

        auto result = system->getDevice().acquireNextImageKHR(
            system->getSwapchainInfo().swapchain.get(), 
            0x7FFFFFFF, 
            system->getSynchronization().GetNextImageSemaphore(0),
            nullptr
        );
        if (result.result == vk::Result::eTimeout) {
            SDL_LogError(0, "Timed out waiting for next frame.");
            return -1;
        }
        uint32_t index = result.value;

        CommandBuffer & cb = system->GetGraphicsCommandBufferWaitAndReset(index, 0x7fffffff);
        system->getDevice().resetFences({fence});

        SDL_LogVerbose(0, 
            "Frame number %u, return value %d.", 
            result.value, static_cast<int32_t>(result.result));
        cb.BeginRenderPass(rp, system->getSwapchainInfo().extent, index);
        cb.BindPipelineProgram(p);
        vk::Rect2D scissor{{0, 0}, system->getSwapchainInfo().extent};
        cb.SetupViewport(system->getSwapchainInfo().extent.width, system->getSwapchainInfo().extent.height, scissor);
        cb.Draw();
        cb.End();
        cb.SubmitToQueue(system->getQueueInfo().graphicsQueue, system->getSynchronization());

        vk::PresentInfoKHR info{};
        auto semaphores = system->getSynchronization().GetCommandBufferSigningSignals(0);
        info.setWaitSemaphores(semaphores);
        std::array<vk::SwapchainKHR, 1> swapchains {system->getSwapchainInfo().swapchain.get()};
        info.setSwapchains(swapchains);
        info.setPImageIndices(&index);
        system->getQueueInfo().presentQueue.presentKHR(info);
    }

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
