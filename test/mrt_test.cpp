#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Core/Functional/SDLWindow.h"
#include "UserInterface/GUISystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

constexpr glm::mat4 EYE4 = glm::mat4(1.0f);

struct LowerPlaneMeshAsset : public MeshAsset {
    LowerPlaneMeshAsset() {
        this->m_submeshes.resize(1);
        this->m_submeshes[0] = {
            .m_indices = {0, 3, 2, 0, 2, 1},
            .m_positions =
                {
                    {0.5f, -0.5f, 0.0f},
                    {0.5f, 0.5f, 0.0f},
                    {-0.5f, 0.5f, 0.0f},
                    {-0.5f, -0.5f, 0.0f},
                },
            .m_attributes_basic = {
                {.color = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 0.0f}},
                {.color = {1.0f, 0.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 1.0f}},
                {.color = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 1.0f}},
                {.color = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 0.0f}}
            },
        };
    }
};

std::shared_ptr<MaterialTemplateAsset> ConstructMaterialTemplate() {
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto vs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/debug_writethrough.vert.spv.asset");
    auto fs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/debug_writethrough_mrt.frag.spv.asset");
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);

    test_asset->name = "Writethrough";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {
        ImageUtils::ImageFormat::R8G8B8A8UNorm,
        ImageUtils::ImageFormat::R8G8B8A8UNorm,
        ImageUtils::ImageFormat::R8G8B8A8UNorm,
        ImageUtils::ImageFormat::R8G8B8A8UNorm
    };
    using CBP = PipelineProperties::ColorBlendingProperties;
    CBP cbp;
    cbp.color_op = cbp.alpha_op = CBP::BlendOperation::None;
    mtspp.attachments.color_blending = {cbp, cbp, cbp, cbp};
    mtspp.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<std::shared_ptr<AssetRef>>{vs_ref, fs_ref};

    test_asset->properties.properties[0] = mtspp;

    return test_asset;
}

RenderGraph BuildRenderGraph(
    RenderSystem *rsys,
    RenderTargetTexture *color_1,
    RenderTargetTexture *color_2,
    RenderTargetTexture *color_3,
    RenderTargetTexture *color_4,
    RenderTargetTexture *depth,
    MaterialInstance *material,
    HomogeneousMesh *mesh
) {
    using IAT = Engine::AccessHelper::ImageAccessType;
    RenderGraphBuilder rgb{*rsys};
    rgb.UseImage(*color_1, IAT::ColorAttachmentWrite);
    rgb.UseImage(*color_2, IAT::ColorAttachmentWrite);
    rgb.UseImage(*color_3, IAT::ColorAttachmentWrite);
    rgb.UseImage(*color_4, IAT::ColorAttachmentWrite);
    rgb.UseImage(*depth, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPassWithoutRT([rsys, color_1, color_2, color_3, color_4, depth, material, mesh](GraphicsCommandBuffer &gcb) {
        auto extent = rsys->GetSwapchain().GetExtent();
        gcb.BeginRendering(
            {
                {color_1, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
                {color_2, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
                {color_3, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
                {color_4, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
            },
            {depth,
             nullptr,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            extent
        );

        gcb.SetupViewport(extent.width, extent.height, {{0, 0}, extent});
        gcb.BindMaterial(*material, 0);
        // Push model matrix...
        vk::CommandBuffer rcb = gcb.GetCommandBuffer();
        rcb.pushConstants(
            material->GetTemplate().GetPipelineLayout(0),
            vk::ShaderStageFlagBits::eVertex,
            0,
            ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
            reinterpret_cast<const void *>(&EYE4)
        );
        gcb.DrawMesh(*mesh);

        gcb.EndRendering();
    });
    return rgb.BuildRenderGraph();
}

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
    cmc->GetAssetManager()->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();

    // Prepare material
    cmc->GetAssetManager()->LoadBuiltinAssets();
    auto test_asset = ConstructMaterialTemplate();

    auto test_asset_ref = std::make_shared<AssetRef>(test_asset);
    auto test_template = std::make_shared<MaterialTemplate>(*rsys);
    test_template->Instantiate(*test_asset_ref->cas<MaterialTemplateAsset>());
    auto test_material_instance = std::make_shared<MaterialInstance>(*rsys, test_template);

    // Prepare mesh
    auto test_mesh_asset = std::make_shared<LowerPlaneMeshAsset>();
    auto test_mesh_asset_ref = std::make_shared<AssetRef>(test_mesh_asset);
    HomogeneousMesh test_mesh{rsys, test_mesh_asset_ref, 0};

    // Submit scene data
    const auto &global_pool = rsys->GetGlobalConstantDescriptorPool();
    struct {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
    } camera_mats;
    ConstantData::PerSceneStruct scene{
        1,
        glm::vec4{-5.0f, -5.0f, -5.0f, 0.0f},
        glm::vec4{1.0, 1.0, 1.0, 0.0},
    };
    for (uint32_t i = 0; i < 3; i++) {
        auto ptr = global_pool.GetPerSceneConstantMemory(i);
        memcpy(ptr, &scene, sizeof scene);
        global_pool.FlushPerSceneConstantMemory(i);
        auto camera_ptr = global_pool.GetPerCameraConstantMemory(i, 0);
        std::memcpy(camera_ptr, &camera_mats, sizeof camera_mats);
        global_pool.FlushPerCameraConstantMemory(i, 0);
    }

    // Prepare attachments
    RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT,
        .multisample = 1,
        .is_cube_map = false
    };
    auto depth = RenderTargetTexture::Create(*rsys, desc, Texture::SamplerDesc{}, "Depth Attachment");
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    auto color_1 = RenderTargetTexture::Create(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Position)");
    auto color_2 = RenderTargetTexture::Create(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Vertex color)");
    auto color_3 = RenderTargetTexture::Create(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Normal)");
    auto color_4 = RenderTargetTexture::Create(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Texcoord)");

    auto asys = cmc->GetAssetManager();

    RenderGraph rg{BuildRenderGraph(
        rsys.get(), color_1.get(), color_2.get(), color_3.get(), color_4.get(), depth.get(), test_material_instance.get(), &test_mesh)
    };

    bool quited = false;
    int color = 0;
    while (max_frame_count--) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key == SDLK_G) {
                    color = (color + 1) % 4;
                }
            }
        }

        // Repeat submission to test for synchronization problems
        rsys->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(test_mesh);

        auto index = rsys->StartFrame();
        rg.Execute();

        vk::Image to_be_present;
        switch(color) {
            case 0:
                to_be_present = color_1->GetImage();
                break;
            case 1:
                to_be_present = color_2->GetImage();
                break;
            case 2:
                to_be_present = color_3->GetImage();
                break;
            default:
                to_be_present = color_4->GetImage();
                break;
        }

        rsys->GetFrameManager().StageBlitComposition(
            to_be_present,
            vk::Extent2D{color_1->GetTextureDescription().width, color_1->GetTextureDescription().height},
            rsys->GetSwapchain().GetExtent()
        );
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    return 0;
}
