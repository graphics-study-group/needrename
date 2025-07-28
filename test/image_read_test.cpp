#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "stb_image.h"

#include "Functional/SDLWindow.h"
#include "MainClass.h"
#include "cmake_config.h"

#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include <Framework/object/GameObject.h>

#include "Render/FullRenderSystem.h"

using namespace Engine;

class TestHomoMesh : public HomogeneousMesh {
public:
    TestHomoMesh(std::weak_ptr<RenderSystem> system) : HomogeneousMesh(system) {
        this->m_positions = {{-0.5f, -0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}};
        this->m_attributes = {
            {.color = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}},
            {.color = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 1.0f}},
            {.color = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {1.0f, 1.0f}},
            {.color = {1.0f, 0.0f, 1.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {1.0f, 0.0f}}
        };
        this->m_indices = {2, 1, 0, 3, 2, 0};
    }
};

int main(int, char *[]) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_LogInfo(0, "Loading mesh...");

    StartupOptions opt;
    opt.resol_x = 1920;
    opt.resol_y = 1080;
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto render_system = cmc->GetRenderSystem();

    // Try render the mesh
    RenderTargetSetup rts{render_system};
    rts.CreateFromSwapchain();
    rts.SetClearValues(
        {vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
         vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}}
    );

    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 144;

    TestHomoMesh mesh{render_system};
    mesh.Prepare();

    int tex_width, tex_height, tex_channel;
    std::filesystem::path image_path{ENGINE_ROOT_DIR};
    image_path /= "assets/bunny/bunny.png";
    stbi_uc *image_data = stbi_load(image_path.string().c_str(), &tex_width, &tex_height, &tex_channel, 4);
    assert(image_data);
    AllocatedImage2DTexture texture{render_system};
    texture.Create(tex_width, tex_height, ImageUtils::ImageFormat::R8G8B8A8SRGB);

    TestMaterialWithSampler material{render_system, texture};

    do {

        render_system->WaitForFrameBegin(in_flight_frame_id);
        GraphicsCommandBuffer &cb = render_system->GetGraphicsCommandBuffer(in_flight_frame_id);

        uint32_t index = render_system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);
        assert(index < 3);

        // Repeatly uploading these data to test synchronization.
        render_system.CommitTextureImage(
            texture, reinterpret_cast<std::byte *>(image_data), tex_width * tex_height * 4
        );

        cb.Begin();
        vk::Extent2D extent{render_system->GetSwapchain().GetExtent()};
        cb.BeginRendering(rts, extent, index);

        cb.BindMaterial(material, 0);
        vk::Rect2D scissor{{0, 0}, render_system->GetSwapchain().GetExtent()};
        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.DrawMesh(mesh, glm::mat4{1.0f});
        cb.EndRendering();
        cb.End();

        cb.Submit();

        render_system->Present(index, in_flight_frame_id);

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
    } while (total_test_frame--);

    stbi_image_free(image_data);
    render_system->WaitForIdle();

    return 0;
}
