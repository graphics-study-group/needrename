#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

#include "Functional/SDLWindow.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"

using namespace Engine;
namespace sch = std::chrono;

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

constexpr uint32_t MAXIMUM_FRAME_COUNT = 14400;

int main(int, char **) {
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto system = cmc->GetRenderSystem();

    RenderTargetSetup rts{system};
    rts.CreateFromSwapchain();
    rts.SetClearValues(
        {vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
         vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}}
    );

    TestMaterialWithTransform material{system};

    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = MAXIMUM_FRAME_COUNT;

    TestHomoMesh mesh{system};

    ConstantData::PerCameraStruct transform{glm::mat4{1.0f}, glm::mat4{1.0f}};

    while (total_test_frame--) {

        if (mesh.NeedCommitment()) {
            system->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(mesh);
        }

        if (total_test_frame > MAXIMUM_FRAME_COUNT / 3 * 2) {
            transform.proj_matrix = glm::rotate(transform.proj_matrix, 0.002f, glm::vec3{0.0f, 0.0f, 1.0f});
        } else if (total_test_frame > MAXIMUM_FRAME_COUNT / 3) {
            transform.view_matrix = glm::rotate(transform.view_matrix, -0.002f, glm::vec3{0.0f, 0.0f, 1.0f});
        } else {
            glm::mat4 old_transform = mesh.GetModelTransform();
            mesh.SetModelTransform(glm::rotate(old_transform, 0.002f, glm::vec3{0.0f, 0.0f, 1.0f}));
        }

        system->WaitForFrameBegin(in_flight_frame_id);
        GraphicsCommandBuffer &cb = system->GetGraphicsCommandBuffer(in_flight_frame_id);

        uint32_t index = system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);
        assert(index < 3);

        system->WritePerCameraConstants(transform, in_flight_frame_id);

        cb.Begin();
        vk::Extent2D extent{system->GetSwapchain().GetExtent()};
        cb.BeginRendering(rts, extent, index);

        cb.BindMaterial(material, 0);
        vk::Rect2D scissor{{0, 0}, system->GetSwapchain().GetExtent()};
        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.DrawMesh(mesh);
        cb.EndRendering();
        cb.End();

        cb.Submit();

        system->Present(index, in_flight_frame_id);

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
    }
    system->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    return 0;
}
