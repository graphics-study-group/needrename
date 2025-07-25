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
    auto cs_ref = asys->GetNewAssetRef("~/shaders/test_compute.comp.spv.asset");
    asys->LoadAssetImmediately(cs_ref);

    auto cs = cs_ref->cas<ShaderAsset>();
    auto ret = ShaderUtils::ReflectSpirvDataCompute(cs->binary);

    assert(ret.desc.names.find("outputImage") != ret.desc.names.end() && ret.desc.names["outputImage"] == 0);
    assert(
        ret.desc.vars[0].set == 0 && ret.desc.vars[0].binding == 1
        && ret.desc.vars[0].type == ShaderVariableProperty::Type::StorageImage
    );

    auto rsys = cmc->GetRenderSystem();

    auto color = std::make_shared<Engine::Texture>(*rsys);
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
    color->CreateTexture(desc, "Color Compute Test");
    ComputeStage cstage{*rsys};
    cstage.InstantiateFromRef(cs_ref);
    cstage.SetDescVariable(
        cstage.GetVariableIndex("outputImage").value().first, std::const_pointer_cast<const Texture>(color)
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
            *color, ComputeContext::ImageComputeAccessType::ShaderRandomWrite, ComputeContext::ImageAccessType::None
        );
        auto ccb = dynamic_cast<ComputeCommandBuffer &>(ccontext.GetCommandBuffer());

        ccontext.PrepareCommandBuffer();
        ccb.BindComputeStage(cstage);
        ccb.DispatchCompute(1280 / 16 + 1, 720 / 16 + 1, 1);

        // We need this barrier to transfer image to color attachment layout for presenting.
        auto gcontext = rsys->GetFrameManager().GetGraphicsContext();
        gcontext.UseImage(
            *color,
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::ShaderRandomWrite
        );
        gcontext.PrepareCommandBuffer();
        ccontext.GetCommandBuffer().End();

        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageBlitComposition(
            color->GetImage(),
            vk::Extent2D{color->GetTextureDescription().width, color->GetTextureDescription().height},
            rsys->GetSwapchain().GetExtent()
        );
        rsys->CompleteFrame();

        SDL_Delay(5);
    }

    rsys->WaitForIdle();
    return 0;
}
