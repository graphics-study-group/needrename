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

constexpr glm::mat4 EYE4{glm::mat4(1.0)};

struct LowerPlaneMeshAsset : public MeshAsset {
    LowerPlaneMeshAsset() {
        this->m_submeshes.resize(1);
        this->m_submeshes[0] = {
            .m_indices = {0, 3, 2, 0, 2, 1},
            .m_positions =
                {
                    {1.0f, -1.0f, 0.5f},
                    {1.0f, 1.0f, 0.5f},
                    {-1.0f, 1.0f, 0.5f},
                    {-1.0f, -1.0f, 0.5f},
                },
            .m_attributes_basic = {
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 0.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 1.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 1.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 0.0f}}
            },
        };
    }
};

struct HighPlaneMeshAsset : public MeshAsset {
    HighPlaneMeshAsset() {
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
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 0.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 1.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 1.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 0.0f}}
            },
        };
    }
};

std::array<std::shared_ptr<MaterialTemplateAsset>, 2> ConstructMaterialTemplate() {
    std::array<std::shared_ptr<MaterialTemplateAsset>, 2> templates {
        std::make_shared<MaterialTemplateAsset>(),
        std::make_shared<MaterialTemplateAsset>()
    };
    
    auto shadow_map_vs_ref =
        MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/shadowmap.vert.spv.asset");
    auto vs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.vert.spv.asset");
    auto fs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.frag.spv.asset");
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(shadow_map_vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);

    templates[0]->name = "Blinn-Phong Lit";
    templates[1]->name = "Shadow map pass";

    MaterialTemplateSinglePassProperties shadow_map_pass{}, lit_pass{};
    shadow_map_pass.shaders.shaders = std::vector{shadow_map_vs_ref};
    shadow_map_pass.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    lit_pass.shaders.shaders = std::vector{vs_ref, fs_ref};
    lit_pass.attachments.color = std::vector{ImageUtils::ImageFormat::R8G8B8A8UNorm};
    lit_pass.attachments.color_blending = std::vector{PipelineProperties::ColorBlendingProperties{}};

    templates[0]->properties = lit_pass;
    templates[1]->properties = shadow_map_pass;

    return templates;
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

    // Prepare texture
    auto test_texture_asset = std::make_shared<Image2DTextureAsset>();
    test_texture_asset->LoadFromFile(std::string(ENGINE_ASSETS_DIR) + "/bunny/bunny.png");
    auto allocated_image_texture = ImageTexture::CreateUnique(*rsys, *test_texture_asset);

    // Prepare mesh
    auto test_mesh_asset = std::make_shared<LowerPlaneMeshAsset>();
    auto test_mesh_asset_ref = std::make_shared<AssetRef>(test_mesh_asset);
    HomogeneousMesh test_mesh{rsys, test_mesh_asset_ref, 0};

    auto test_mesh_asset_2 = std::make_shared<HighPlaneMeshAsset>();
    auto test_mesh_asset_2_ref = std::make_shared<AssetRef>(test_mesh_asset_2);
    HomogeneousMesh test_mesh_2{rsys, test_mesh_asset_2_ref, 0};

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
    Engine::RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    std::shared_ptr color = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color attachment");
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    std::shared_ptr depth = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Depth attachment");
    desc.width = desc.height = 2048;
    std::shared_ptr shadow = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Depth attachment");
    std::shared_ptr blank_color = Engine::ImageTexture::CreateUnique(
        *rsys, 
        ImageTexture::ImageTextureDesc{
            .dimensions = 2,
            .width = 16,
            .height = 16,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
            .is_cube_map = false
        }, 
        Texture::SamplerDesc{}, 
        "Blank color"
    );

    // Prepare material
    cmc->GetAssetManager()->LoadBuiltinAssets();
    auto test_asset = ConstructMaterialTemplate();
    auto test_asset_ref = std::make_shared<AssetRef>(test_asset[0]);
    auto test_template = std::make_shared<MaterialTemplate>(*rsys);
    test_template->Instantiate(*test_asset_ref->cas<MaterialTemplateAsset>());
    auto test_material_instance = std::make_shared<MaterialInstance>(*rsys, test_template);
    test_material_instance->AssignVectorVariable(
        "ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0)
    );
    test_material_instance->AssignVectorVariable(
        "specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0)
    );
    test_material_instance->AssignTexture(
        "base_tex", *blank_color
    );

    rsys->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(test_mesh);
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(test_mesh_2);
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
        *allocated_image_texture, test_texture_asset->GetPixelData(), test_texture_asset->GetPixelDataSize()
    );

    RenderGraphBuilder rgb{*rsys};
    rgb.RegisterImageAccess(*color);
    rgb.RegisterImageAccess(*depth);
    rgb.RegisterImageAccess(*shadow);

    using IAT = AccessHelper::ImageAccessType;
    rgb.UseImage(*shadow, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPassWithoutRT(
        [rsys, shadow, test_template, test_material_instance, &test_mesh, &test_mesh_2](GraphicsCommandBuffer &gcb) {
            vk::Extent2D shadow_map_extent{2048, 2048};
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            gcb.BeginRendering(
                {nullptr},
                {shadow.get(), nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
                shadow_map_extent,
                "Shadowmap Pass"
            );
            gcb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
            gcb.BindMaterial(*test_material_instance);

            vk::CommandBuffer rcb = gcb.GetCommandBuffer();
            rcb.pushConstants(
                test_template->GetPipelineLayout(),
                vk::ShaderStageFlagBits::eVertex,
                0,
                ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
                reinterpret_cast<const void *>(&EYE4)
            );
            gcb.DrawMesh(test_mesh);
            gcb.DrawMesh(test_mesh_2);
            gcb.EndRendering();
        }
    );

    rgb.UseImage(*shadow, IAT::ShaderRead);
    rgb.UseImage(*color, IAT::ColorAttachmentWrite);
    rgb.UseImage(*depth, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPass(
        {color.get(), nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
        {depth.get(),
         nullptr,
         AttachmentUtils::LoadOperation::Clear,
         AttachmentUtils::StoreOperation::DontCare,
         AttachmentUtils::DepthClearValue{1.0f, 0U}},
        [rsys, test_material_instance, test_template, &test_mesh, &test_mesh_2](GraphicsCommandBuffer &gcb) {
            vk::Extent2D extent{rsys->GetSwapchain().GetExtent()};
            vk::Rect2D scissor{{0, 0}, extent};
            gcb.SetupViewport(extent.width, extent.height, scissor);
            gcb.BindMaterial(*test_material_instance);
            // Push model matrix...
            vk::CommandBuffer rcb = gcb.GetCommandBuffer();
            rcb.pushConstants(
                test_template->GetPipelineLayout(),
                vk::ShaderStageFlagBits::eVertex,
                0,
                ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
                reinterpret_cast<const void *>(&EYE4)
            );
            gcb.DrawMesh(test_mesh);
            gcb.DrawMesh(test_mesh_2);
        },
        "Lit pass"
    );
    auto rg{rgb.BuildRenderGraph()};

    bool quited = false;

    int64_t frame_count = 0;
    while (++frame_count) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
        }

        switch (frame_count % 3) {
        case 0:
            rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color, {1.0f, 0.0f, 0.0f, 0.0f});
            break;
        case 1:
            rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color, {0.0f, 1.0f, 0.0f, 0.0f});
            break;
        case 2:
            rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color, {0.0f, 0.0f, 1.0f, 0.0f});
            break;
        }

        auto index = rsys->StartFrame();
        assert(index < 3);

        rg.Execute();
        rsys->GetFrameManager().StageCopyComposition(color->GetImage());
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited || frame_count > max_frame_count) break;
    }

    rsys->WaitForIdle();

    return 0;
}
