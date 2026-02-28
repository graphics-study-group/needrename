#include "MainClass.h"
#include "Render/FullRenderSystem.h"
using namespace Engine;

void dummy_compute_pass(ComputeCommandBuffer &, const RenderGraph2 &) {
}

void dummy_graphics_pass(GraphicsCommandBuffer &, const RenderGraph2 &) {
}

int main() {
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Vulkan Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto rgb = RenderGraphBuilder2{*cmc->GetRenderSystem()};

    auto rttd = RenderTargetTexture::RenderTargetTextureDesc{
        .dimensions = 2,
        .width = 1280, .height = 720, 
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
    };
    auto gbuffer = rgb.RequestRenderTargetTexture(rttd, {}, "G-Buffer");
    auto fbuffer = rgb.RequestRenderTargetTexture(rttd, {}, "Main buffer");

    rgb.AddPass(
        RenderGraphPassBuilder{*cmc->GetRenderSystem()}
            .SetName("Compute pass")
            .SetGlobalAccess({MemoryAccessTypeBufferBits::ShaderRandomWrite})
            .SetComputePassFunction(dummy_compute_pass)
            .Get()
    );

    rgb.AddPass(
        RenderGraphPassBuilder{*cmc->GetRenderSystem()}
            .SetName("Main pass")
            .UseImage(gbuffer, MemoryAccessTypeImageBits::ShaderSampledRead)
            .AppendColorAttachment({fbuffer, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store})
            .SetRasterizerPassFunction(dummy_graphics_pass)
            .Get()
    );

    rgb.AddPass(
        RenderGraphPassBuilder{*cmc->GetRenderSystem()}
            .SetName("GBuffer pass")
            .SetGlobalAccess({MemoryAccessTypeBufferBits::IndexRead, MemoryAccessTypeBufferBits::VertexRead})
            .AppendColorAttachment({gbuffer, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store})
            .SetRasterizerPassFunction(dummy_graphics_pass)
            .Get()
    );

    rgb.AddPass(
        RenderGraphPassBuilder{*cmc->GetRenderSystem()}
            .SetName("Post processing compute")
            .UseImage(fbuffer, MemoryAccessTypeImageBits::ShaderRandomRead)
            .SetComputePassFunction(dummy_compute_pass)
            .Get()
    );

    auto rg = rgb.BuildRenderGraph();
}
