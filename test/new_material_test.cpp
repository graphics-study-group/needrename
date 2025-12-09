#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Mesh/PlaneMeshAsset.h"
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

struct LowerPlaneMeshAsset : public PlaneMeshAsset {
    LowerPlaneMeshAsset() : PlaneMeshAsset() {
        this->m_submeshes.resize(1);
        this->m_submeshes[0].positions = MeshAsset::Submesh::Attributes{
            .type = MeshAsset::Submesh::Attributes::AttributeType::Floatx3,
            .attribf = {1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 0.5f, -1.0f, 1.0f, 0.5f, -1.0f, -1.0f, 0.5f},
        };

        // Flip normal to upwards in clip space.
        for (size_t i = 0; i < this->m_submeshes[0].normal.attribf.size(); i += 3) {
            this->m_submeshes[0].normal.attribf[i + 2] = -1.0f;
        }
    }
};

std::pair<std::shared_ptr<MaterialLibraryAsset>, std::shared_ptr<MaterialTemplateAsset>> ConstructMaterial() {
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(
        MainClass::GetInstance()->GetAssetDatabase()
    );
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto lib_asset = std::make_shared<MaterialLibraryAsset>();
    auto vs_ref = adb->GetNewAssetRef("~/shaders/blinn_phong.vert.asset");
    auto fs_ref = adb->GetNewAssetRef("~/shaders/blinn_phong.frag.asset");
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

    test_asset->properties = mtspp;

    lib_asset->m_name = "Blinn-Phong";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = std::make_shared<AssetRef>(test_asset);
    lib_asset->material_bundle[""] = ref;

    return std::make_pair(lib_asset, test_asset);
}

RenderGraph BuildRenderGraph(
    RenderSystem *rsys,
    RenderTargetTexture *color,
    RenderTargetTexture *depth,
    MaterialInstance *material,
    HomogeneousMesh *mesh,
    RenderTargetTexture *blurred = nullptr,
    ComputeStage *kernel = nullptr
) {
    using IAT = Engine::AccessHelper::ImageAccessType;
    RenderGraphBuilder rgb{*rsys};
    rgb.UseImage(*color, IAT::ColorAttachmentWrite);
    rgb.UseImage(*depth, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPassWithoutRT([rsys, color, depth, material, mesh](GraphicsCommandBuffer &gcb) {
        auto extent = rsys->GetSwapchain().GetExtent();
        gcb.BeginRendering(
            AttachmentUtils::AttachmentDescription{
                color, 
                nullptr, 
                AttachmentUtils::LoadOperation::Clear, 
                AttachmentUtils::StoreOperation::Store
            },
            AttachmentUtils::AttachmentDescription{
                depth,
                nullptr,
                AttachmentUtils::LoadOperation::Clear,
                AttachmentUtils::StoreOperation::DontCare,
                AttachmentUtils::DepthClearValue{1.0f, 0U}
            },
            extent
        );

        gcb.SetupViewport(extent.width, extent.height, {{0, 0}, extent});
        auto tpl = material->GetLibrary().FindMaterialTemplate("", VertexAttribute::GetDefaultBasicVertexAttribute());
        assert(tpl);
        gcb.BindMaterial(*material, *tpl);
        // Push model matrix...
        vk::CommandBuffer rcb = gcb.GetCommandBuffer();
        rcb.pushConstants(
            material->GetLibrary().FindMaterialTemplate("", VertexAttribute::GetDefaultBasicVertexAttribute())->GetPipelineLayout(),
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof (RenderSystemState::RendererManager::RendererDataStruct),
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
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();
    // Prepare texture
    auto test_texture_asset = std::make_shared<Image2DTextureAsset>();
    test_texture_asset->LoadFromFile(std::string(ENGINE_ASSETS_DIR) + "/bunny/bunny.png");
    std::shared_ptr allocated_image_texture = ImageTexture::CreateUnique(*rsys, *test_texture_asset);

    // Prepare material
    auto test_asset = ConstructMaterial();

    // // Do not delete this code, it is a useful tool for generating asset files.
    // Engine::Serialization::Archive archive;
    // archive.prepare_save();
    // test_asset.first->save_asset_to_archive(archive);
    // archive.save_to_file(std::string(ENGINE_ASSETS_DIR) + "/mla.asset");
    // SDL_Log("Saved MaterialLibraryAsset to %s", (std::string(ENGINE_ASSETS_DIR) + "/mla.asset").c_str());
    // archive.clear();
    // archive.prepare_save();
    // test_asset.second->save_asset_to_archive(archive);
    // archive.save_to_file(std::string(ENGINE_ASSETS_DIR) + "/mta.asset");
    // SDL_Log("Saved MaterialTemplateAsset to %s", (std::string(ENGINE_ASSETS_DIR) + "/mta.asset").c_str());

    auto test_library = std::make_shared<Materials::BlinnPhongLibrary>(*rsys);
    test_library->Instantiate(*test_asset.first);
    auto test_material_instance = std::make_shared<Materials::BlinnPhongInstance>(*rsys, test_library);
    test_material_instance->SetAmbient(glm::vec4(0.0, 0.0, 0.0, 0.0));
    test_material_instance->SetSpecular(glm::vec4(1.0, 1.0, 1.0, 64.0));
    test_material_instance->SetBaseTexture(allocated_image_texture);

    // Prepare mesh
    auto test_mesh_asset = std::make_shared<LowerPlaneMeshAsset>();
    auto test_mesh_asset_ref = std::make_shared<AssetRef>(test_mesh_asset);
    HomogeneousMesh test_mesh{rsys->GetAllocatorState(), test_mesh_asset_ref, 0};

    // Submit scene data
    rsys->GetCameraManager().WriteCameraMatrices(glm::mat4{1.0f}, glm::mat4{1.0f});
    rsys->GetSceneDataManager().SetLightDirectionalNonShadowCasting(
        0, 
        glm::vec3{-5.0f, -5.0f, -5.0f}, 
        glm::vec3{1.0, 1.0, 1.0}
    );
    rsys->GetSceneDataManager().SetLightCountNonShadowCasting(1);

    // Prepare attachments
    
    RenderTargetTexture::RenderTargetTextureDesc desc{
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
    std::shared_ptr color = RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment");
    std::shared_ptr postproc = RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Gaussian Blurred");
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    std::shared_ptr depth = RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Depth Attachment");

    auto asys = cmc->GetAssetManager();
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    auto cs_ref = adb->GetNewAssetRef("~/shaders/gaussian_blur.comp.asset");
    asys->LoadAssetImmediately(cs_ref);
    ComputeStage cstage{*rsys};
    cstage.Instantiate(*cs_ref->cas<ShaderAsset>());
    cstage.AssignTexture("inputImage", color);
    cstage.AssignTexture("outputImage", postproc);

    RenderGraph nonblur{BuildRenderGraph(rsys.get(), color.get(), depth.get(), test_material_instance.get(), &test_mesh)};
    RenderGraph blur{BuildRenderGraph(
        rsys.get(), color.get(), depth.get(), test_material_instance.get(), &test_mesh, postproc.get(), &cstage
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
