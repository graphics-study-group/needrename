#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Functional/SDLWindow.h"
#include "GUI/GUISystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

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

std::shared_ptr<MaterialTemplateAsset> ConstructMaterialTemplate() {
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto shadow_map_vs_ref =
        MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/shadowmap.vert.spv.asset");
    auto vs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.vert.spv.asset");
    auto fs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.frag.spv.asset");
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(shadow_map_vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);

    test_asset->name = "Blinn-Phong Lit";

    MaterialTemplateSinglePassProperties shadow_map_pass{}, lit_pass{};
    shadow_map_pass.shaders.shaders = std::vector{shadow_map_vs_ref};
    shadow_map_pass.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    lit_pass.shaders.shaders = std::vector{vs_ref, fs_ref};

    test_asset->properties.properties[0] = shadow_map_pass;
    test_asset->properties.properties[1] = lit_pass;

    return test_asset;
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
    auto allocated_image_texture = std::make_shared<SampledTextureInstantiated>(*rsys);
    allocated_image_texture->Instantiate(*test_texture_asset);

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

    glm::mat4 eye4 = glm::mat4(1.0f);

    // Prepare attachments
    auto color = std::make_shared<Engine::Texture>(*rsys);
    auto depth = std::make_shared<Engine::Texture>(*rsys);
    auto shadow = std::make_shared<Engine::SampledTexture>(*rsys);
    auto blank_color = std::make_shared<Engine::SampledTexture>(*rsys);
    Engine::Texture::TextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .format = Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB,
        .type = Engine::ImageUtils::ImageType::ColorAttachment,
        .mipmap_levels = 1,
        .array_layers = 1,
        .is_cube_map = false
    };
    color->CreateTexture(desc, "Color attachment");
    desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
    desc.type = Engine::ImageUtils::ImageType::DepthImage;
    depth->CreateTexture(desc, "Depth attachment");
    desc.width = desc.height = 2048;
    desc.type = Engine::ImageUtils::ImageType::SampledDepthImage;
    shadow->CreateTextureAndSampler(desc, {}, "Shadow map");
    desc.width = desc.height = 16;
    desc.format = Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB;
    desc.type = Engine::ImageUtils::ImageType::TextureImage;
    blank_color->CreateTextureAndSampler(desc, {}, "Blank color");

    Engine::AttachmentUtils::AttachmentDescription color_att, depth_att, shadow_att;
    color_att.texture = color.get();
    color_att.load_op = AttachmentUtils::LoadOperation::Clear;
    color_att.store_op = AttachmentUtils::StoreOperation::Store;
    depth_att.texture = depth.get();
    depth_att.load_op = AttachmentUtils::LoadOperation::Clear;
    depth_att.store_op = AttachmentUtils::StoreOperation::DontCare;
    shadow_att.texture = shadow.get();
    shadow_att.load_op = AttachmentUtils::LoadOperation::Clear;
    shadow_att.store_op = AttachmentUtils::StoreOperation::Store;

    // Prepare material
    cmc->GetAssetManager()->LoadBuiltinAssets();
    auto test_asset = ConstructMaterialTemplate();
    auto test_asset_ref = std::make_shared<AssetRef>(test_asset);
    auto test_template = std::make_shared<MaterialTemplate>(*rsys);
    test_template->Instantiate(*test_asset_ref->cas<MaterialTemplateAsset>());
    auto test_material_instance = std::make_shared<MaterialInstance>(*rsys, test_template);
    test_material_instance->WriteDescriptors(0);
    test_material_instance->WriteUBOUniform(
        1, test_template->GetVariableIndex("ambient_color", 1).value().first, glm::vec4(0.0, 0.0, 0.0, 0.0)
    );
    test_material_instance->WriteUBOUniform(
        1, test_template->GetVariableIndex("specular_color", 1).value().first, glm::vec4(1.0, 1.0, 1.0, 64.0)
    );
    test_material_instance->WriteTextureUniform(
        1, test_template->GetVariableIndex("base_tex", 1).value().first, blank_color
    );
    // test_material_instance->WriteTextureUniform(1, test_template->GetVariableIndex("shadowmap_tex", 1).value().first,
    // shadow);
    test_material_instance->WriteDescriptors(1);

    rsys->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(test_mesh);
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(test_mesh_2);
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
        *allocated_image_texture, test_texture_asset->GetPixelData(), test_texture_asset->GetPixelDataSize()
    );

    RenderTargetBinding shadow_pass_binding, lit_pass_binding;
    shadow_pass_binding.SetDepthAttachment(shadow_att);
    lit_pass_binding.SetColorAttachment(color_att);
    lit_pass_binding.SetDepthAttachment(depth_att);

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
        auto context = rsys->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer &cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());
        assert(index < 3);

        cb.Begin("Main Render Loop");
        // Shadow map pass
        context.UseImage(
            *shadow,
            GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.PrepareCommandBuffer();
        {
            vk::Extent2D shadow_map_extent{2048, 2048};
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            cb.BeginRendering(shadow_pass_binding, shadow_map_extent, "Shadowmap Pass");
            cb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
            cb.BindMaterial(*test_material_instance, 0);

            vk::CommandBuffer rcb = cb.GetCommandBuffer();
            rcb.pushConstants(
                test_template->GetPipelineLayout(0),
                vk::ShaderStageFlagBits::eVertex,
                0,
                ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
                reinterpret_cast<const void *>(&eye4)
            );
            cb.DrawMesh(test_mesh);
            cb.DrawMesh(test_mesh_2);
            cb.EndRendering();
        }

        // Lit pass
        context.UseImage(
            *shadow,
            GraphicsContext::ImageGraphicsAccessType::ShaderRead,
            GraphicsContext::ImageAccessType::DepthAttachmentWrite
        );
        context.UseImage(
            *color,
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.UseImage(
            *depth,
            GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.PrepareCommandBuffer();
        {
            vk::Extent2D extent{rsys->GetSwapchain().GetExtent()};
            vk::Rect2D scissor{{0, 0}, extent};
            cb.BeginRendering(lit_pass_binding, extent, "Lit Pass");
            cb.SetupViewport(extent.width, extent.height, scissor);
            cb.BindMaterial(*test_material_instance, 1);
            // Push model matrix...
            vk::CommandBuffer rcb = cb.GetCommandBuffer();
            rcb.pushConstants(
                test_template->GetPipelineLayout(0),
                vk::ShaderStageFlagBits::eVertex,
                0,
                ConstantData::PerModelConstantPushConstant::PUSH_RANGE_SIZE,
                reinterpret_cast<const void *>(&eye4)
            );
            cb.DrawMesh(test_mesh);
            cb.DrawMesh(test_mesh_2);
            cb.EndRendering();
        }

        cb.End();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageCopyComposition(color->GetImage());
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited || frame_count > max_frame_count) break;
    }

    rsys->WaitForIdle();

    return 0;
}
