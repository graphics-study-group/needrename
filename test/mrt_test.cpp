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
    }
};

std::pair<std::shared_ptr<MaterialLibraryAsset>, std::shared_ptr<MaterialTemplateAsset>> ConstructMaterial() {
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(
        MainClass::GetInstance()->GetAssetDatabase()
    );
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto test_lib_asset = std::make_shared<MaterialLibraryAsset>();
    auto vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/debug_writethrough.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef({*adb, "~/shaders/debug_writethrough_mrt.frag.asset"});
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

    test_asset->properties = mtspp;

    test_lib_asset->m_name = "MRT Writethrough";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = std::make_shared<AssetRef>(test_asset);
    test_lib_asset->material_bundle[""] = ref;

    return std::make_pair(test_lib_asset, test_asset);
}

std::unique_ptr<RenderGraph> BuildRenderGraph(
    RenderSystem *rsys,
    RenderTargetTexture *color_1,
    RenderTargetTexture *color_2,
    RenderTargetTexture *color_3,
    RenderTargetTexture *color_4,
    RenderTargetTexture *depth,
    MaterialInstance *material,
    HomogeneousMesh *mesh
) {
    using IAT = Engine::MemoryAccessTypeImageBits;
    RenderGraphBuilder rgb{*rsys};
    rgb.UseImage(*color_1, IAT::ColorAttachmentWrite);
    rgb.UseImage(*color_2, IAT::ColorAttachmentWrite);
    rgb.UseImage(*color_3, IAT::ColorAttachmentWrite);
    rgb.UseImage(*color_4, IAT::ColorAttachmentWrite);
    rgb.UseImage(*depth, IAT::DepthStencilAttachmentWrite);
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
        VertexAttribute attribute;
        attribute.SetAttribute(VertexAttributeSemantic::Position, VertexAttributeType::SFloat32x3);
        attribute.SetAttribute(VertexAttributeSemantic::Color, VertexAttributeType::SFloat32x3);
        attribute.SetAttribute(VertexAttributeSemantic::Normal, VertexAttributeType::SFloat32x3);
        attribute.SetAttribute(VertexAttributeSemantic::Texcoord0, VertexAttributeType::SFloat32x2);
        auto tpl = material->GetLibrary().FindMaterialTemplate("", attribute);
        assert(tpl);
        gcb.BindMaterial(*material, *tpl);
        // Push model matrix...
        vk::CommandBuffer rcb = gcb.GetCommandBuffer();
        rcb.pushConstants(
            material->GetLibrary().FindMaterialTemplate("", attribute)->GetPipelineLayout(),
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof(RenderSystemState::RendererManager::RendererDataStruct),
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
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();

    // Prepare material
    auto test_asset = ConstructMaterial();
    auto test_asset_ref = std::make_shared<AssetRef>(test_asset.first);
    auto test_library = std::make_shared<MaterialLibrary>(*rsys);
    test_library->Instantiate(*test_asset_ref->cas<MaterialLibraryAsset>());
    auto test_material_instance = std::make_shared<MaterialInstance>(*rsys, *test_library);

    // Prepare mesh
    auto test_mesh_asset = std::make_shared<LowerPlaneMeshAsset>();
    auto test_mesh_asset_ref = std::make_shared<AssetRef>(test_mesh_asset);
    HomogeneousMesh test_mesh{rsys->GetAllocatorState(), test_mesh_asset_ref, 0};

    // Submit scene data
    rsys->GetCameraManager().WriteCameraMatrices(glm::mat4{1.0f}, glm::mat4{1.0f});
    rsys->GetSceneDataManager().SetLightDirectionalNonShadowCasting(0, glm::vec3{-5.0f, -5.0f, -5.0f}, glm::vec3{1.0, 1.0, 1.0});
    rsys->GetSceneDataManager().SetLightCountNonShadowCasting(1);

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
    auto depth = RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Depth Attachment");
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    std::array colors {
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Position)"),
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Vertex color)"),
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Normal)"),
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Texcoord)")
    };

    auto asys = cmc->GetAssetManager();

    auto rg{BuildRenderGraph(
        rsys.get(), colors[0].get(), colors[1].get(), colors[2].get(), colors[3].get(), depth.get(), test_material_instance.get(), &test_mesh)
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

        // Test readback on the next color attachment for fixed layout
        auto buf = rsys->GetFrameManager().EnqueuePostGraphicsImageReadback(
            *colors[(color + 1) % 4], 0, 0
        );

        rg->AddExternalOutputDependency(*colors[(color + 1) % 4], MemoryAccessTypeImageBits::TransferRead);
        rg->Execute();

        rsys->CompleteFrame(
            *colors[color],
            colors[color]->GetTextureDescription().width, 
            colors[color]->GetTextureDescription().height
        );

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    return 0;
}
