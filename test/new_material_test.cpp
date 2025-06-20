#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "GUI/GUISystem.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Render/FullRenderSystem.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;


struct LowerPlaneMeshAsset : public MeshAsset {
    LowerPlaneMeshAsset() {
        this->m_submeshes.resize(1);
        this->m_submeshes[0] = {
            .m_indices = {0, 3, 2, 0, 2, 1},
            .m_positions = {
                {0.5f, -0.5f, 0.0f}, 
                {0.5f, 0.5f, 0.0f}, 
                {-0.5f, 0.5f, 0.0f},
                {-0.5f, -0.5f, 0.0f},
            },
            .m_attributes = {
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 0.0f}}, 
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {1.0f, 1.0f}}, 
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 1.0f}},
                {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}, .texcoord1 = {0.0f, 0.0f}}
            },
        };
    }
};

std::shared_ptr<MaterialTemplateAsset> ConstructMaterialTemplate()
{
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto vs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.vert.spv.asset");
    auto fs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/blinn_phong.frag.spv.asset");
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);
    
    test_asset->name = "Blinn-Phong";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {
        ImageUtils::ImageFormat::R8G8B8A8UNorm
    };
    mtspp.attachments.color_ops = {
        AttachmentUtils::AttachmentOp{}
    };
    using CBP = PipelineProperties::ColorBlendingProperties;
    CBP cbp;
    cbp.color_op = cbp.alpha_op = CBP::BlendOperation::Add;
    cbp.src_color = CBP::BlendFactor::SrcAlpha;
    cbp.dst_color = CBP::BlendFactor::OneMinusSrcAlpha;
    cbp.src_alpha = CBP::BlendFactor::One;
    cbp.dst_alpha = CBP::BlendFactor::Zero;
    mtspp.attachments.color_blending = {
        cbp
    };
    mtspp.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<std::shared_ptr<AssetRef>>{vs_ref, fs_ref};

    test_asset->properties.properties[0] = mtspp;

    return test_asset;
}

int main(int argc, char ** argv)
{
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
    auto test_asset_ref = std::make_shared<AssetRef>(test_asset);
    auto test_template = std::make_shared<Materials::BlinnPhongTemplate>(*rsys);
    test_template->InstantiateFromRef(test_asset_ref);
    auto test_material_instance = std::make_shared<Materials::BlinnPhongInstance>(*rsys, test_template);
    test_material_instance->SetAmbient(glm::vec4(0.0, 0.0, 0.0, 0.0));
    test_material_instance->SetSpecular(glm::vec4(1.0, 1.0, 1.0, 64.0));
    test_material_instance->SetBaseTexture(allocated_image_texture);
    test_material_instance->WriteDescriptors(0);

    // Prepare mesh
    auto test_mesh_asset = std::make_shared<LowerPlaneMeshAsset>();
    auto test_mesh_asset_ref = std::make_shared<AssetRef>(test_mesh_asset);
    HomogeneousMesh test_mesh{rsys, test_mesh_asset_ref, 0};
    test_mesh.Prepare();

    // Submit scene data
    const auto & global_pool = rsys->GetGlobalConstantDescriptorPool();
    struct {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
    } camera_mats;
    ConstantData::PerSceneStruct scene {
        1,
        glm::vec4{-5.0f, -5.0f, -5.0f, 0.0f},
        glm::vec4{1.0, 1.0, 1.0, 0.0},
    };
    for (uint32_t i = 0; i < 3; i++) {
        auto ptr = global_pool.GetPerSceneConstantMemory(i);
        memcpy(ptr, &scene, sizeof scene);
        global_pool.FlushPerSceneConstantMemory(i); 
        std::byte * camera_ptr = global_pool.GetPerCameraConstantMemory(i);
        std::memcpy(camera_ptr, &camera_mats, sizeof camera_mats);
        global_pool.FlushPerCameraConstantMemory(i);
    }

    glm::mat4 eye4 = glm::mat4(1.0f);

    // Prepare attachments
    Engine::Texture depth{*rsys};
    auto color = std::make_shared<Texture>(*rsys);
    auto postproc = std::make_shared<Texture>(*rsys);
    Engine::Texture::TextureDesc desc {
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

    Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
    color_att.image = color->GetImage();
    color_att.image_view = color->GetImageView();
    color_att.load_op = vk::AttachmentLoadOp::eClear;
    color_att.store_op = vk::AttachmentStoreOp::eStore;

    depth_att.image = depth.GetImage();
    depth_att.image_view = depth.GetImageView();
    depth_att.load_op = vk::AttachmentLoadOp::eClear;
    depth_att.store_op = vk::AttachmentStoreOp::eDontCare;

    auto asys = cmc->GetAssetManager();
    auto cs_ref = asys->GetNewAssetRef("~/shaders/gaussian_blur.comp.spv.asset");
    asys->LoadAssetImmediately(cs_ref);
    ComputeStage cstage{*rsys};
    cstage.InstantiateFromRef(cs_ref);
    cstage.SetDescVariable(
        cstage.GetVariableIndex("inputImage").value().first,
        std::const_pointer_cast<const Texture>(color)
    );
    cstage.SetDescVariable(
        cstage.GetVariableIndex("outputImage").value().first,
        std::const_pointer_cast<const Texture>(postproc)
    );

    bool quited = false;
    bool has_gaussian_blur = true;
    while(max_frame_count--) {
        SDL_Event event;
        while(SDL_PollEvent(&event) != 0) {
            switch(event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            case SDL_EVENT_KEY_UP:
                if(event.key.key == SDLK_G) {
                    has_gaussian_blur = !has_gaussian_blur;
                }
            }
        }

        // Repeat submission to test for synchronization problems
        rsys->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(test_mesh);
        rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(*allocated_image_texture, test_texture_asset->GetPixelData(), test_texture_asset->GetPixelDataSize());

        auto index = rsys->StartFrame();
        auto gcontext = rsys->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer & cb = dynamic_cast<GraphicsCommandBuffer &>(gcontext.GetCommandBuffer());

        assert(index < 3);
    
        cb.Begin();

        gcontext.UseImage(*color, GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite, GraphicsContext::ImageAccessType::None);
        gcontext.UseImage(depth, GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite, GraphicsContext::ImageAccessType::None);
        gcontext.PrepareCommandBuffer();

        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        vk::Rect2D scissor{{0, 0}, extent};
        cb.BeginRendering(color_att, depth_att, extent);

        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.BindMaterial(*test_material_instance, 0);
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

        cb.EndRendering();
        if (has_gaussian_blur) {
            auto ccontext = rsys->GetFrameManager().GetComputeContext();
            ccontext.UseImage(
                *color,
                Engine::ComputeContext::ImageComputeAccessType::ShaderReadRandomWrite, 
                Engine::ComputeContext::ImageAccessType::ColorAttachmentWrite
            );
            ccontext.UseImage(
                *postproc,
                Engine::ComputeContext::ImageComputeAccessType::ShaderRandomWrite, 
                Engine::ComputeContext::ImageAccessType::None
            );
            ccontext.PrepareCommandBuffer();
            auto ccb = dynamic_cast<ComputeCommandBuffer &>(ccontext.GetCommandBuffer());
            ccb.BindComputeStage(cstage);
            ccb.DispatchCompute(
                postproc->GetTextureDescription().width / 16 + 1, 
                postproc->GetTextureDescription().height / 16 + 1, 
                1
            );

            gcontext.UseImage(*postproc, 
                GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite, 
                Engine::ComputeContext::ImageAccessType::ShaderRandomWrite
            );
            gcontext.PrepareCommandBuffer();
        }
        

        cb.End();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageBlitComposition(
            has_gaussian_blur ? postproc->GetImage() : color->GetImage(), 
            vk::Extent2D{
                postproc->GetTextureDescription().width,
                postproc->GetTextureDescription().height
            }, 
            rsys->GetSwapchain().GetExtent()
        );
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    rsys->ClearComponent();

    return 0;
}
