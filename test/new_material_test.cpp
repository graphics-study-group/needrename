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
    auto vs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.vert.spv.asset");
    auto fs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.frag.spv.asset");
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);

    test_asset->name = "Blinn-Phong";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {ImageUtils::ImageFormat::R8G8B8A8UNorm};
    using CBP = PipelineProperties::ColorBlendingProperties;
    CBP cbp;
    cbp.color_op = cbp.alpha_op = CBP::BlendOperation::Add;
    cbp.src_color = CBP::BlendFactor::SrcAlpha;
    cbp.dst_color = CBP::BlendFactor::OneMinusSrcAlpha;
    cbp.src_alpha = CBP::BlendFactor::One;
    cbp.dst_alpha = CBP::BlendFactor::Zero;
    mtspp.attachments.color_blending = {cbp};
    mtspp.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<std::shared_ptr<AssetRef>>{vs_ref, fs_ref};

    test_asset->properties.properties[0] = mtspp;

    return test_asset;
}

RenderGraph BuildRenderGraph(
    RenderSystem *rsys,
    Texture *color,
    Texture *depth,
    MaterialInstance *material,
    HomogeneousMesh *mesh,
    Texture *blurred = nullptr,
    ComputeStage *kernel = nullptr
) {
    using IAT = Engine::AccessHelper::ImageAccessType;
    RenderGraphBuilder rgb{*rsys};
    rgb.UseImage(*color, IAT::ColorAttachmentWrite);
    rgb.UseImage(*depth, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPassWithoutRT([rsys, color, depth, material, mesh](GraphicsCommandBuffer &gcb) {
        auto extent = rsys->GetSwapchain().GetExtent();
        gcb.BeginRendering(
            {color, nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store},
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

    if (blurred && kernel) {
        rgb.RegisterImageAccess(*blurred);
        rgb.UseImage(*color, IAT::ShaderReadRandomWrite);
        rgb.UseImage(*blurred, IAT::ShaderRandomWrite);

        rgb.RecordComputePass([blurred, kernel](ComputeCommandBuffer &ccb) {
            ccb.BindComputeStage(*kernel);
            ccb.DispatchCompute(
                blurred->GetTextureDescription().width / 16 + 1, blurred->GetTextureDescription().height / 16 + 1, 1
            );
        });

        rgb.UseImage(*blurred, IAT::ColorAttachmentWrite);
        rgb.RecordSynchronization();
    }
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
    // Prepare texture
    auto test_texture_asset = std::make_shared<Image2DTextureAsset>();
    test_texture_asset->LoadFromFile(std::string(ENGINE_ASSETS_DIR) + "/bunny/bunny.png");
    auto allocated_image_texture = std::make_shared<SampledTextureInstantiated>(*rsys);
    allocated_image_texture->Instantiate(*test_texture_asset);

    // Prepare material
    cmc->GetAssetManager()->LoadBuiltinAssets();
    auto test_asset = ConstructMaterialTemplate();

    // Engine::Serialization::Archive archive;
    // archive.prepare_save();
    // test_asset->save_asset_to_archive(archive);
    // archive.save_to_file(std::string(ENGINE_ASSETS_DIR) + "/test_asset.asset");
    // SDL_Log("Saved asset to %s", (std::string(ENGINE_ASSETS_DIR) + "/test_asset.asset").c_str());

    auto test_asset_ref = std::make_shared<AssetRef>(test_asset);
    auto test_template = std::make_shared<Materials::BlinnPhongTemplate>(*rsys);
    test_template->Instantiate(*test_asset_ref->cas<MaterialTemplateAsset>());
    auto test_material_instance = std::make_shared<Materials::BlinnPhongInstance>(*rsys, test_template);
    test_material_instance->SetAmbient(glm::vec4(0.0, 0.0, 0.0, 0.0));
    test_material_instance->SetSpecular(glm::vec4(1.0, 1.0, 1.0, 64.0));
    test_material_instance->SetBaseTexture(allocated_image_texture);
    test_material_instance->WriteDescriptors(0);

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
    Engine::Texture depth{*rsys};
    auto color = std::make_shared<Texture>(*rsys);
    auto postproc = std::make_shared<Texture>(*rsys);
    Engine::Texture::TextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .format = Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm,
        .type = Engine::ImageUtils::ImageType::ColorGeneral,
        .mipmap_levels = 1,
        .array_layers = 1,
        .is_cube_map = false
    };
    color->CreateTexture(desc, "Color Attachment");
    postproc->CreateTexture(desc, "GaussianBlurred");
    desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
    desc.type = Engine::ImageUtils::ImageType::DepthImage;
    depth.CreateTexture(desc, "Depth Attachment");

    auto asys = cmc->GetAssetManager();
    auto cs_ref = asys->GetNewAssetRef("~/shaders/gaussian_blur.comp.spv.asset");
    asys->LoadAssetImmediately(cs_ref);
    ComputeStage cstage{*rsys};
    cstage.Instantiate(*cs_ref->cas<ShaderAsset>());
    cstage.SetDescVariable(
        cstage.GetVariableIndex("inputImage").value().first, std::const_pointer_cast<const Texture>(color)
    );
    cstage.SetDescVariable(
        cstage.GetVariableIndex("outputImage").value().first, std::const_pointer_cast<const Texture>(postproc)
    );

    RenderGraph nonblur{BuildRenderGraph(rsys.get(), color.get(), &depth, test_material_instance.get(), &test_mesh)};
    RenderGraph blur{BuildRenderGraph(
        rsys.get(), color.get(), &depth, test_material_instance.get(), &test_mesh, postproc.get(), &cstage
    )};

    bool quited = false;
    bool has_gaussian_blur = true;
    while (max_frame_count--) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key == SDLK_G) {
                    has_gaussian_blur = !has_gaussian_blur;
                }
            }
        }

        // Repeat submission to test for synchronization problems
        rsys->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(test_mesh);
        rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
            *allocated_image_texture, test_texture_asset->GetPixelData(), test_texture_asset->GetPixelDataSize()
        );

        auto index = rsys->StartFrame();
        if (has_gaussian_blur) {
            blur.Execute();
        } else {
            nonblur.Execute();
        }
        rsys->GetFrameManager().StageBlitComposition(
            has_gaussian_blur ? postproc->GetImage() : color->GetImage(),
            vk::Extent2D{postproc->GetTextureDescription().width, postproc->GetTextureDescription().height},
            rsys->GetSwapchain().GetExtent()
        );
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    return 0;
}
