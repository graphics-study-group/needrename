#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/Pipeline.h"
#include "Render/Pipeline/Framebuffers.h"
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
    Pipeline p{system};
    ShaderModule fragModule {system};
    ShaderModule vertModule {system};
    fragModule.CreateShaderModule(readFile("shader/debug_fragment_color.frag.spv"));
    vertModule.CreateShaderModule(readFile("shader/debug_vertex_color.vert.spv"));

    p.CreatePipeline(rp.GetSubpass(0), pl, {
        fragModule.GetStageCreateInfo(vk::ShaderStageFlagBits::eFragment),
        vertModule.GetStageCreateInfo(vk::ShaderStageFlagBits::eVertex)
        });

    Framebuffers fb{rp.CreateFramebuffers()};

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
