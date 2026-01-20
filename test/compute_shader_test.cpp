#include <SDL3/SDL.h>
#include <iostream>

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Shader/ShaderAsset.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include <cmake_config.h>

using namespace Engine;

int main(int argc, char *argv[]) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Compute Shader Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto asys = cmc->GetAssetManager();
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto cs = std::make_shared<ShaderAsset>();
    cs->LoadFromFile(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/fluid.comp.0.glsl", ShaderAsset::ShaderType::Compute);

    auto rsys = cmc->GetRenderSystem();

    Engine::RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R32G32B32A32SFloat,
        .multisample = 1,
        .is_cube_map = false
    };

    std::shared_ptr color_input = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Compute Input");
    std::shared_ptr color_output = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Compute Output");
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    std::shared_ptr color_present = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Present");
    
    ComputeStage cstage{*rsys};
    cstage.Instantiate(*cs);
    cstage.AssignTexture("outputImage", color_output);
    cstage.AssignTexture("inputImage", color_input);
    cstage.AssignTexture("outputColorImage", color_present);

    uint64_t frame_count = 0;
    while (++frame_count) {
        if (frame_count > max_frame_count) break;

        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                rsys->WaitForIdle();
                return 0;
            }
        }

        rsys->StartFrame();
        auto ccontext = rsys->GetFrameManager().GetComputeContext();
        ccontext.GetCommandBuffer().Begin();
        ccontext.UseImage(
            *color_input, 
            ComputeContext::ImageComputeAccessType::ShaderReadRandomWrite, 
            frame_count == 1 ? ComputeContext::ImageAccessType::None : ComputeContext::ImageAccessType::TransferWrite
        );
        ccontext.UseImage(
            *color_output, ComputeContext::ImageComputeAccessType::ShaderRandomWrite, ComputeContext::ImageAccessType::None
        );
        ccontext.UseImage(
            *color_present, ComputeContext::ImageComputeAccessType::ShaderRandomWrite, ComputeContext::ImageAccessType::None
        );
        auto ccb = dynamic_cast<ComputeCommandBuffer &>(ccontext.GetCommandBuffer());

        ccontext.PrepareCommandBuffer();
        cstage.AssignScalarVariable("UBO::frame_count", static_cast<uint32_t>(frame_count));
        ccb.BindComputeStage(cstage);
        ccb.DispatchCompute(1280 / 16 + 1, 720 / 16 + 1, 1);

        // Blit back history info
        auto gcontext = rsys->GetFrameManager().GetGraphicsContext();
        auto & tcontext = static_cast<TransferContext &>(gcontext);
        tcontext.UseImage(
            *color_output,
            GraphicsContext::ImageTransferAccessType::TransferRead,
            GraphicsContext::ImageAccessType::ShaderRandomWrite
        );
        tcontext.UseImage(
            *color_input,
            GraphicsContext::ImageTransferAccessType::TransferWrite,
            GraphicsContext::ImageAccessType::ShaderReadRandomWrite
        );
        tcontext.PrepareCommandBuffer();
        auto & tcb = dynamic_cast<TransferCommandBuffer &>(tcontext.GetCommandBuffer());
        tcb.BlitColorImage(*color_output, *color_input);

        // We need this barrier to transfer image to color attachment layout for presenting.
        gcontext.UseImage(
            *color_present,
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::ShaderRandomWrite
        );
        gcontext.PrepareCommandBuffer();
        ccontext.GetCommandBuffer().End();

        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->CompleteFrame(
            *color_present,
            color_present->GetTextureDescription().width, 
            color_present->GetTextureDescription().height
        );

        SDL_Delay(15);
    }

    rsys->WaitForIdle();
    return 0;
}
