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
#include "Asset/Material/MaterialTemplateAsset.h"

using namespace Engine;
namespace sch = std::chrono;

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
    auto gsys = cmc->GetGUISystem();

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
            gsys->ProcessEvent(&event);
        }
        
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
        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    return 0;
}
