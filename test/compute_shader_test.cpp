#include <SDL3/SDL.h>
#include <iostream>

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Framework/MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Render/Shader/ShaderAsset.h"
#include <cmake_config.h>

using namespace Engine;

RGTextureHandle g_color_in_handle;

auto BuildRenderGraph(
    RenderSystem &rsys,
    RenderTargetTexture &color_in,
    RenderTargetTexture &color_out,
    RenderTargetTexture &color_present,
    Rhi::ComputeStage &compute,
    Rhi::ComputeResourceBinding &cbinding
) {
    RenderGraphBuilder rgb{rsys};
    auto ci = rgb.ImportExternalResource(color_in, Rhi::MemoryAccessTypeImageBits::TransferWrite);
    auto co = rgb.ImportExternalResource(color_out);
    auto cp = rgb.ImportExternalResource(color_present);
    rgb.AddPass(
        RenderGraphPassBuilder{rsys}
            .SetName("Fluid simulation")
            .UseImage(ci, Rhi::MemoryAccessTypeImageBits::ShaderRandomRead)
            .UseImage(co, Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite)
            .UseImage(cp, Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite)
            .SetAffinity(RenderGraphPassAffinity::Compute)
            .SetPassFunction([&](CommandBuffer &cb, const RenderGraph &) -> void {
                cb.BindComputeStage(compute);
                cb.BindComputeResource(cbinding);
                cb.DispatchCompute(1280 / 16 + 1, 720 / 16 + 1, 1);
            })
            .Get()
    );

    rgb.AddPass(
        RenderGraphPassBuilder{rsys}
            .SetName("Blitting")
            .UseImage(ci, Rhi::MemoryAccessTypeImageBits::TransferWrite)
            .UseImage(co, Rhi::MemoryAccessTypeImageBits::TransferRead)
            .SetPassFunction([&](CommandBuffer &cb, const RenderGraph &) -> void {
                cb.BlitColorImage(color_out, color_in);
            })
            .Get()
    );
    g_color_in_handle = ci;
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
    cs->LoadFromFile(
        std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/fluid.comp.0.glsl", ShaderAsset::ShaderType::Compute
    );

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

    std::shared_ptr color_input = Engine::RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Compute Input"
    );
    std::shared_ptr color_output = Engine::RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Compute Output"
    );
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    std::shared_ptr color_present = Engine::RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Present"
    );

    Rhi::ComputeStage cstage{rsys->GetDeviceContext()};
    cstage.Instantiate(cs->binary, cs->m_name);
    auto &cbinding = cstage.AllocateResourceBinding(RenderSystemState::FrameManager::FRAMES_IN_FLIGHT);
    cbinding.GetShaderResourceBinding().BindTexture("outputImage", *color_output);
    cbinding.GetShaderResourceBinding().BindTexture("inputImage", *color_input);
    cbinding.GetShaderResourceBinding().BindTexture("outputColorImage", *color_present);

    auto rg = BuildRenderGraph(*rsys, *color_input, *color_output, *color_present, cstage, cbinding);

    uint64_t frame_count = 0;
    while (++frame_count) {
        if (frame_count > static_cast<uint64_t>(max_frame_count)) break;

        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                rsys->WaitForIdle();
                return 0;
            }
        }

        if (rsys->StartFrame() == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after retry — skip this frame.
            continue;
        }
        cbinding.GetStructuredBuffer().SetVariable<uint32_t>("UBO::frame_count", static_cast<uint32_t>(frame_count));

        if (frame_count == 1) rg->AddExternalInputDependency(g_color_in_handle, Rhi::MemoryAccessTypeImageBits::None);
        rsys->GetFrameManager().BeginMainCommandBuffer();
        rg->RecordIntoMainCommandBuffer(*rsys);

        rsys->CompleteFrame(*color_present, Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite);

        SDL_Delay(15);
    }

    rsys->WaitForIdle();
    return 0;
}
