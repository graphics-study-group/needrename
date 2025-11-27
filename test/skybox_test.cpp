#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Asset/Texture/ImageCubemapAsset.h"
#include "UserInterface/GUISystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

// We will align the system to the world system for now, which means
// X+ -> right, Y+ -> front, Z+ -> up.
const std::array<std::filesystem::path, 6> CUBEMAP_FACES = {
    std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "skybox" / "skybox_R_tonemapped.png",
    std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "skybox" / "skybox_L_tonemapped.png",
    std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "skybox" / "skybox_F_tonemapped.png",
    std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "skybox" / "skybox_B_tonemapped.png",
    std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "skybox" / "skybox_U_tonemapped.png",
    std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "skybox" / "skybox_D_tonemapped.png"
};

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    auto rsys = cmc->GetRenderSystem();

    // Load skybox cubemap
    auto cubemap = std::make_shared<ImageCubemapAsset>();
    cubemap->LoadFromFile(CUBEMAP_FACES);

    auto skybox_texture = ImageTexture::CreateUnique(
        *rsys,
        ImageTexture::ImageTextureDesc{
            .dimensions = 2,
            .width = 512,
            .height = 512,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 6,
            .format = ImageTexture::ITFormat::R8G8B8A8SRGB,
            .is_cube_map = true
        },
        ImageUtils::SamplerDesc{},
        "Skybox"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
        *skybox_texture,
        cubemap->GetPixelData(),
        cubemap->GetPixelDataSize()
    );

    // Dummy texture for presenting
    auto rt = RenderTargetTexture::CreateUnique(
        *rsys,
        RenderTargetTexture::RenderTargetTextureDesc{
            .dimensions = 2,
            .width = 16,
            .height = 16,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RTTFormat::R8G8B8A8SNorm
        },
        ImageUtils::SamplerDesc{},
        "Dummy render texture"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*rt, {0.5, 0.5, 0.5, 1.0});

    bool quited{false};
    while (max_frame_count--) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
        }

        rsys->StartFrame();
        auto cb = rsys->GetFrameManager().GetRawMainCommandBuffer();
        vk::CommandBufferBeginInfo cbbi{};
        cb.begin(cbbi);
        cb.end();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageBlitComposition(rt->GetImage(), {16, 16}, rsys->GetSwapchain().GetExtent());
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    return 0;
}
