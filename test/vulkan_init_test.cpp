#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Material/TestMaterial.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/CommandBuffer.h"
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

class TestMeshComponent : public MeshComponent {
    Transform transform;
public: 
    TestMeshComponent(std::weak_ptr <RenderSystem> system, std::shared_ptr<Material> mat) 
    : MeshComponent(std::weak_ptr<GameObject>(), system), transform() {
        m_materials.push_back(mat);
        m_submeshes.push_back(std::make_shared<TestHomoMesh>(system));
        m_submeshes[0]->Prepare();
        if (m_submeshes[0]->NeedCommitment()) {
            auto & tcb = system.lock()->GetTransferCommandBuffer();
            tcb.Begin();
            tcb.CommitVertexBuffer(*m_submeshes[0]);
            tcb.End();
            tcb.SubmitAndExecute();
        }

        transform.SetPosition(glm::vec3{1.0, 0.0, 0.0});
    }

    ~TestMeshComponent() {
        m_materials.clear();
        m_submeshes.clear();
    }

    Transform GetWorldTransform() const override {
        return transform;
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

    RenderTargetSetup rts{system};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });
    

    std::shared_ptr material = std::make_shared<TestMaterial>(system);

    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 60;

    std::shared_ptr tmc = std::make_shared<TestMeshComponent>(system, material);
    system->RegisterComponent(tmc);
    
    uint64_t start_timer = SDL_GetPerformanceCounter();
    while(total_test_frame--) {
        
        auto frame_start_timer = sch::high_resolution_clock::now();

        system->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = system->GetGraphicsCommandBuffer(in_flight_frame_id);
        uint32_t index = system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);

        assert(index < 3);
    
        cb.Begin();
        vk::Extent2D extent {system->GetSwapchain().GetExtent()};
        cb.BeginRenderPass(rts, extent, index);
        system->DrawMeshes(in_flight_frame_id);
        cb.End();
        cb.Submit();

        system->Present(index, in_flight_frame_id);

        auto submission_end_timer = sch::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> total;
        total = submission_end_timer - frame_start_timer;
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
    system->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
