#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Render/ConstantData/PerCameraConstants.h"
#include "Render/Material/TestMaterial.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/Framebuffers.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/PremadePipeline/DefaultPipeline.h"
#include "Render/Pipeline/PremadePipeline/SingleRenderPassWithDepth.h"
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
    system->EnableDepthTesting();

    SingleRenderPassWithDepth rp{system};
    rp.CreateRenderPass();
    rp.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });
    rp.CreateFramebuffersFromSwapchain();

    TestMaterial material{system, rp};

    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 60;

    TestHomoMesh mesh{system};
    mesh.Prepare();

    ConstantData::PerCameraStruct transform {

    };
    
    while(total_test_frame--) {

        system->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = system->GetGraphicsCommandBuffer(in_flight_frame_id);

        uint32_t index = system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);
        assert(index < 3);
    
        cb.Begin();
        if (mesh.NeedCommitment()) {
            cb.CommitVertexBuffer(mesh);
        }
        vk::Extent2D extent {system->GetSwapchain().GetExtent()};
        cb.BeginRenderPass(rp, extent, index);

        cb.BindMaterial(material, 0);
        vk::Rect2D scissor{{0, 0}, system->GetSwapchain().GetExtent()};
        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.DrawMesh(mesh);
        cb.End();

        cb.SubmitToQueue(system->getQueueInfo().graphicsQueue, system->getSynchronization());

        system->Present(index, in_flight_frame_id);

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
    }
    system->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
