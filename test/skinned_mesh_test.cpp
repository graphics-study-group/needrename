#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Render/Material/BoneWeightVisualizer.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Renderer/SkinnedHomogeneousMesh.h"

using namespace Engine;
namespace sch = std::chrono;

Engine::MainClass * cmc;

class TestHomoMesh : public SkinnedHomogeneousMesh {
public:
    TestHomoMesh(std::weak_ptr<RenderSystem> system) : SkinnedHomogeneousMesh(system) {
        this->m_positions = {
            {0.0f, 0.0f, 0.5f},
            {0.5f, 0.5f, 0.0f},
            {-0.5f, 0.5f, 0.0f},
            {-0.5f, -0.5f, 0.0f},
            {0.5f, -0.5f, 0.0f},
            {0.0f, 0.0f, -0.5f}
        };
        this->m_attributes = {
            {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}}
        };
        this->m_bones = {
            {{1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}},
            {{1, 2, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}},
            {{1, 2, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}},
            {{1, 2, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}},
            {{1, 2, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}},
            {{2, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}},
        };
        this->m_indices = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1, 5, 1, 0, 5, 2, 1, 5, 3, 2, 5, 4, 3};
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
            tcb.CommitVertexBuffer(dynamic_cast<SkinnedHomogeneousMesh &>(*(m_submeshes[0])));
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

    auto rsys = cmc->GetRenderSystem();
    rsys->EnableDepthTesting();

    RenderTargetSetup rts{rsys};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });
    
    // Setup material
    std::shared_ptr material = std::make_shared<BoneWeightVisualizer>(rsys);
    material->UpdateUniform({0});

    std::shared_ptr tmc = std::make_shared<TestMeshComponent>(rsys, material);
    rsys->RegisterComponent(tmc);

    // Setup camera
    auto camera_go = std::make_shared<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, 1.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    camera_go->SetTransform(transform);
    auto camera_comp = std::make_shared<CameraComponent>(camera_go);
    camera_comp->set_aspect_ratio(1920.0 / 1080.0);
    camera_go->AddComponent(camera_comp);
    rsys->SetActiveCamera(camera_comp);
    
    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 6000;
    uint64_t start_timer = SDL_GetPerformanceCounter();
    while(total_test_frame--) {
        rsys->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = rsys->GetGraphicsCommandBuffer(in_flight_frame_id);
        uint32_t index = rsys->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);

        assert(index < 3);
    
        cb.Begin();
        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        vk::Rect2D scissor{{0, 0}, extent};
        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.BeginRendering(rts, extent, index);
        // rsys->DrawMeshes(in_flight_frame_id);
        cb.BindMaterial(*material, 0, true);
        cb.DrawMesh(*tmc->GetSubmesh(0));
        cb.EndRendering();
        cb.End();
        cb.Submit();

        rsys->Present(index, in_flight_frame_id);
    }
    uint64_t end_timer = SDL_GetPerformanceCounter();
    uint64_t duration = end_timer - start_timer;
    double duration_time = 1.0 * duration / SDL_GetPerformanceFrequency();
    SDL_LogInfo(0, "Took %lf seconds for 200 frames (avg. %lf fps).", duration_time, 200.0 / duration_time);
    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
