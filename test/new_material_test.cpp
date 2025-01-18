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
#include "Render/Material/MaterialTemplate.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Material/MaterialTemplateAsset.h"

using namespace Engine;
namespace sch = std::chrono;


struct TestMeshAsset : public MeshAsset {
    TestMeshAsset() {
        this->m_submeshes.resize(1);
        this->m_submeshes[0] = {
            .m_indices = {0, 1, 2},
            .m_positions = {{0.0f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}},
            .m_attributes = {
                {.color = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}, 
                {.color = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}, 
                {.color = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}
            },
        };
    }
};

struct TestMaterialAsset : public MaterialTemplateAsset {

    std::shared_ptr<ShaderAsset> vertex {}, fragment {};
    std::shared_ptr<AssetRef> vertex_ref {}, fragment_ref {};

    TestMaterialAsset () : MaterialTemplateAsset() {
        vertex = std::make_shared <ShaderAsset> ();
        fragment = std::make_shared <ShaderAsset> ();
    }

    void initalize() {
        this->name = "test material";

        this->vertex->filename = "shader/debug_vertex_trig.vert.spv";
        this->vertex->shaderType = ShaderAsset::ShaderType::Vertex;
        this->fragment->filename = "shader/debug_fragment_color.frag.spv";
        this->fragment->shaderType = ShaderAsset::ShaderType::Fragment;
        vertex_ref = std::make_shared<AssetRef>(vertex);
        fragment_ref = std::make_shared<AssetRef>(fragment);

        MaterialTemplateSinglePassProperties mtspp {};
        std::vector <AssetRef> shaders = { *vertex_ref, *fragment_ref };
        mtspp.shaders.shaders = shaders;
        mtspp.shaders.uniforms = {

        };
        this->properties.properties[0] = mtspp;
    }
};

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

    RenderTargetSetup rts{rsys};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });

    auto test_asset = std::make_shared<TestMaterialAsset>();
    test_asset->initalize();
    auto test_asset_ref = std::make_shared<AssetRef>(test_asset);
    MaterialTemplate material_template{rsys, test_asset_ref};

    auto test_mesh_asset = std::make_shared<TestMeshAsset>();
    auto test_mesh_asset_ref = std::make_shared<AssetRef>(test_mesh_asset);
    HomogeneousMesh test_mesh{rsys, test_mesh_asset_ref, 0};
    test_mesh.Prepare();

    auto & tcb = rsys->GetTransferCommandBuffer();
    tcb.Begin();
    tcb.CommitVertexBuffer(test_mesh);
    tcb.End();
    tcb.SubmitAndExecute();

    uint32_t in_flight_frame_id = 0;
    bool quited = false;
    while(max_frame_count--) {
        SDL_Event event;
        while(SDL_PollEvent(&event) != 0) {
            switch(event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
        }

        rsys->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = rsys->GetGraphicsCommandBuffer(in_flight_frame_id);
        uint32_t index = rsys->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);

        assert(index < 3);
    
        cb.Begin();

        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        vk::Rect2D scissor{{0, 0}, extent};
        cb.BeginRendering(rts, extent, index);

        cb.SetupViewport(extent.width, extent.height, scissor);
        // We haven't design new command buffer yet, so just use raw vulkan functions for testing.
        vk::CommandBuffer rcb = cb.get();
        rcb.bindPipeline(vk::PipelineBindPoint::eGraphics, material_template.GetPipeline(0));
        cb.DrawMesh(test_mesh);

        cb.EndRendering();

        cb.End();
        cb.Submit();

        rsys->Present(index, in_flight_frame_id);

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    return 0;
}
