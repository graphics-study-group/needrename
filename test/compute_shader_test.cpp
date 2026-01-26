#include <SDL3/SDL.h>
#include <iostream>

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Shader/ShaderAsset.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include <cmake_config.h>

using namespace Engine;

auto BuildRenderGraph(
    RenderSystem & rsys,
    RenderTargetTexture & color_in,
    RenderTargetTexture & color_out,
    RenderTargetTexture & color_present,
    ComputeStage & compute
) {
    RenderGraphBuilder rgb{rsys};
    rgb.ImportExternalResource(color_in, MemoryAccessTypeImageBits::TransferWrite);

    rgb.UseImage(color_in, MemoryAccessTypeImageBits::ShaderRandomRead);
    rgb.UseImage(color_out, MemoryAccessTypeImageBits::ShaderRandomWrite);
    rgb.UseImage(color_present, MemoryAccessTypeImageBits::ShaderRandomWrite);
    rgb.RecordComputePass([&] (ComputeCommandBuffer & ccb) -> void {
        ccb.BindComputeStage(compute);
        ccb.DispatchCompute(1280 / 16 + 1, 720 / 16 + 1, 1);
    });

    rgb.UseImage(color_in, MemoryAccessTypeImageBits::TransferWrite);
    rgb.UseImage(color_out, MemoryAccessTypeImageBits::TransferRead);
    rgb.RecordTransferPass([&] (TransferCommandBuffer & tcb) -> void {
        tcb.BlitColorImage(color_out, color_in);
    });

    return rgb.BuildRenderGraph();
}

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

    auto rg = BuildRenderGraph(
        *rsys,
        *color_input,
        *color_output,
        *color_present,
        cstage
    );

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
        cstage.AssignScalarVariable("UBO::frame_count", static_cast<uint32_t>(frame_count));

        if (frame_count == 1) rg->AddExternalInputDependency(*color_input, MemoryAccessTypeImageBits::None);
        rg->Execute();

        rsys->CompleteFrame(
            *color_present,
            MemoryAccessTypeImageBits::ShaderRandomWrite,
            color_present->GetTextureDescription().width, 
            color_present->GetTextureDescription().height
        );

        SDL_Delay(15);
    }

    rsys->WaitForIdle();
    return 0;
}
