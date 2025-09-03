#include <SDL3/SDL.h>
#include <iostream>

#include "Asset/AssetManager/AssetManager.h"
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
    asys->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    asys->LoadBuiltinAssets();
    auto cs_ref = asys->GetNewAssetRef("~/shaders/fluid.comp.spv.asset");
    asys->LoadAssetImmediately(cs_ref);

    auto cs = cs_ref->cas<ShaderAsset>();
    auto ret = ShaderUtils::ReflectSpirvDataCompute(cs->binary);
    assert(ret.inblock.names.find("frame_count") != ret.inblock.names.end() && ret.inblock.names["frame_count"] == 0);
    assert(
        ret.inblock.vars[0].block_location.set == 0 && ret.inblock.vars[0].block_location.binding == 0
        && ret.inblock.vars[0].type == ShaderUtils::InBlockVariableData::Type::Int
        && ret.inblock.vars[0].inblock_location.abs_offset == 0
        && ret.inblock.vars[0].inblock_location.size == 4
    );

    assert(ret.desc.names.find("inputImage") != ret.desc.names.end() && ret.desc.names["inputImage"] == 1);
    assert(
        ret.desc.vars[1].set == 0 && ret.desc.vars[1].binding == 1
        && ret.desc.vars[1].type == ShaderVariableProperty::Type::StorageImage
    );
    assert(ret.desc.names.find("outputImage") != ret.desc.names.end() && ret.desc.names["outputImage"] == 2);
    assert(
        ret.desc.vars[2].set == 0 && ret.desc.vars[2].binding == 2
        && ret.desc.vars[2].type == ShaderVariableProperty::Type::StorageImage
    );
    assert(ret.desc.names.find("outputColorImage") != ret.desc.names.end() && ret.desc.names["outputColorImage"] == 3);
    assert(
        ret.desc.vars[3].set == 0 && ret.desc.vars[3].binding == 3
        && ret.desc.vars[3].type == ShaderVariableProperty::Type::StorageImage
    );
    

    auto rsys = cmc->GetRenderSystem();

    auto color_input = std::make_shared<Engine::Texture>(*rsys);
    auto color_output = std::make_shared<Engine::Texture>(*rsys);
    auto color_present = std::make_shared<Engine::Texture>(*rsys);
    Engine::Texture::TextureDesc desc{
        .dimensions = 2,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .format = Engine::ImageUtils::ImageFormat::R8G8B8A8SNorm,
        .type = Engine::ImageUtils::ImageType::ColorGeneral,
        .mipmap_levels = 1,
        .array_layers = 1,
        .is_cube_map = false
    };
    color_input->CreateTexture(desc, "Color Compute Input");
    color_output->CreateTexture(desc, "Color Compute Output");
    desc.format = Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm;
    color_present->CreateTexture(desc, "Color Present");
    ComputeStage cstage{*rsys};
    cstage.Instantiate(*cs_ref->cas<ShaderAsset>());
    cstage.SetDescVariable(
        cstage.GetVariableIndex("outputImage").value().first, std::const_pointer_cast<const Texture>(color_output)
    );
    cstage.SetDescVariable(
        cstage.GetVariableIndex("inputImage").value().first, std::const_pointer_cast<const Texture>(color_input)
    );
    cstage.SetDescVariable(
        cstage.GetVariableIndex("outputColorImage").value().first, std::const_pointer_cast<const Texture>(color_present)
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
        auto ccontext = rsys->GetFrameManager().GetComputeContext();
        ccontext.GetCommandBuffer().Begin();
        ccontext.UseImage(
            *color_input, ComputeContext::ImageComputeAccessType::ShaderReadRandomWrite, ComputeContext::ImageAccessType::TransferWrite
        );
        ccontext.UseImage(
            *color_output, ComputeContext::ImageComputeAccessType::ShaderRandomWrite, ComputeContext::ImageAccessType::None
        );
        ccontext.UseImage(
            *color_present, ComputeContext::ImageComputeAccessType::ShaderRandomWrite, ComputeContext::ImageAccessType::None
        );
        auto ccb = dynamic_cast<ComputeCommandBuffer &>(ccontext.GetCommandBuffer());

        ccontext.PrepareCommandBuffer();
        cstage.SetInBlockVariable(
            cstage.GetVariableIndex("frame_count").value().first, 
            static_cast<int>(frame_count)
        );
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
        rsys->GetFrameManager().StageBlitComposition(
            color_present->GetImage(),
            vk::Extent2D{
                color_present->GetTextureDescription().width, 
                color_present->GetTextureDescription().height
            },
            rsys->GetSwapchain().GetExtent()
        );
        rsys->CompleteFrame();

        SDL_Delay(15);
    }

    rsys->WaitForIdle();
    return 0;
}
