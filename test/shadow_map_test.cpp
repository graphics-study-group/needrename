#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Mesh/PlaneMeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Framework/object/GameObject.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Framework/world/WorldSystem.h"
#include "Core/Functional/SDLWindow.h"
#include "UserInterface/GUISystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Framework/component/RenderComponent/ObjTestMeshComponent.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

struct LowerPlaneMeshAsset : public PlaneMeshAsset {
    LowerPlaneMeshAsset() {
        this->m_submeshes.resize(1);
        this->m_submeshes[0].positions = MeshAsset::Submesh::Attributes{
            .type = MeshAsset::Submesh::Attributes::AttributeType::Floatx3,
            .attribf = {1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f},
        };

        // Flip normal to upwards in clip space.
        for (size_t i = 0; i < this->m_submeshes[0].normal.attribf.size(); i += 3) {
            this->m_submeshes[0].normal.attribf[i + 2] = -1.0f;
        }
    }
};

class ShadowMapMeshComponent : public ObjTestMeshComponent {
public:
    ShadowMapMeshComponent(
        std::weak_ptr <GameObject> go,
        std::filesystem::path mesh_file_name,
        std::shared_ptr<MaterialInstance> instance
    ) : ObjTestMeshComponent(mesh_file_name, go) {
        auto system = m_system.lock();

        for (size_t i = 0; i < m_submeshes.size(); i++) {
            m_materials.push_back(instance);
        }
    }
};

std::array<std::shared_ptr<MaterialTemplateAsset>, 2> ConstructMaterialTemplate() {
    std::array<std::shared_ptr<MaterialTemplateAsset>, 2> templates {
        std::make_shared<MaterialTemplateAsset>(),
        std::make_shared<MaterialTemplateAsset>()
    };

    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    auto asys = MainClass::GetInstance()->GetAssetManager();

    auto shadow_map_vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/shadowmap.vert.asset"});
    auto vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/blinn_phong.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef({*adb, "~/shaders/blinn_phong.frag.asset"});
    asys->LoadAssetImmediately(shadow_map_vs_ref);
    asys->LoadAssetImmediately(vs_ref);
    asys->LoadAssetImmediately(fs_ref);

    templates[0]->name = "Blinn-Phong Lit";
    templates[1]->name = "Shadow map pass";

    MaterialTemplateSinglePassProperties shadow_map_pass{}, lit_pass{};
    shadow_map_pass.shaders.shaders = std::vector{shadow_map_vs_ref};
    shadow_map_pass.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    shadow_map_pass.rasterizer.depth_bias_constant = 0.05f;
    shadow_map_pass.rasterizer.depth_bias_slope = 1.0f;

    // shadow_map_pass.rasterizer.culling = PipelineProperties::RasterizerProperties::CullingMode::Front;
    lit_pass.shaders.shaders = std::vector{vs_ref, fs_ref};
    lit_pass.attachments.color = std::vector{ImageUtils::ImageFormat::R8G8B8A8UNorm};
    lit_pass.attachments.color_blending = std::vector{PipelineProperties::ColorBlendingProperties{}};
    lit_pass.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;

    templates[0]->properties = lit_pass;
    templates[1]->properties = shadow_map_pass;

    return templates;
}

std::shared_ptr <MaterialLibraryAsset> ConstructMaterialLibrary(std::array<std::shared_ptr<MaterialTemplateAsset>, 2> & templates) {
    std::shared_ptr <MaterialLibraryAsset> lib = std::make_shared<MaterialLibraryAsset>();
    lib->m_name = "Blinn-Phong w. Shadowmap";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = std::make_shared<AssetRef>(templates[0]);
    lib->material_bundle["Lit"] = ref;

    ref.expected_mesh_type = 0;
    ref.material_template = std::make_shared<AssetRef>(templates[1]);
    lib->material_bundle["Shadowmap"] = ref;
    return lib;
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

    // Submit scene data
    Transform transform{};
    transform.SetPosition({0.0f, 0.0f, -5.0f}).SetRotationEuler(glm::vec3{M_PI_2, 0.0f, 0.0f});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(1920.0 / 1080.0);
    camera->m_clipping_far = 1e2;
    camera->UpdateViewMatrix(transform);
    rsys->GetCameraManager().RegisterCamera(camera);
    rsys->GetCameraManager().SetActiveCameraIndex(camera->m_display_id);
    rsys->GetSceneDataManager().SetLightDirectional(
        0, 
        glm::vec3{1.0f, 1.0f, 1.0f}, 
        glm::vec3{1.0f, 1.0f, 1.0f}
    );
    rsys->GetSceneDataManager().SetLightCount(1);

    // Prepare attachments
    Engine::RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2, .width = 1920, .height = 1080,
        .depth = 1, .mipmap_levels = 1, .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    std::shared_ptr color = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color attachment");
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    std::shared_ptr depth = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Depth attachment");
    desc.width = desc.height = 2048;
    std::shared_ptr shadow = Engine::RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Shadowmap Light 0");
    rsys->GetSceneDataManager().SetLightShadowMap(0, shadow);
    auto idesc = ImageTexture::ImageTextureDesc{
        .dimensions = 2, .width = 16, .height = 16, .depth = 1,
        .mipmap_levels = 1, .array_layers = 1,
        .format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
        .is_cube_map = false
    };
    std::shared_ptr blank_color_red = Engine::ImageTexture::CreateUnique(*rsys, idesc, Texture::SamplerDesc{}, "Blank color red");
    std::shared_ptr blank_color_gray = Engine::ImageTexture::CreateUnique(*rsys, idesc, Texture::SamplerDesc{}, "Blank color gray");
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_red, {1.0f, 0.0f, 0.0f, 0.0f});
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_gray, {0.5f, 0.5f, 0.5f, 0.0f});

    // Prepare material
    auto test_template_assets = ConstructMaterialTemplate();
    auto test_library_asset = ConstructMaterialLibrary(test_template_assets);

    // Engine::Serialization::Archive archive;
    // archive.prepare_save();
    // test_template_assets[0]->save_asset_to_archive(archive);
    // archive.save_to_file(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "material_templates" / "BlinnPhongTemplate");
    // archive.clear();
    // archive.prepare_save();
    // test_template_assets[1]->save_asset_to_archive(archive);
    // archive.save_to_file(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "material_templates" / "ShadowMapTemplate");
    // archive.clear();
    // archive.prepare_save();
    // test_library_asset->save_asset_to_archive(archive);
    // archive.save_to_file(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "material_libraries" / "BlinnPhongWithShadowMapLibrary");

    auto test_library_asset_ref = std::make_shared<AssetRef>(test_library_asset);
    auto test_library = std::make_shared<MaterialLibrary>(*rsys);
    test_library->Instantiate(*test_library_asset_ref->cas<MaterialLibraryAsset>());
    auto object_material_instance = std::make_shared<MaterialInstance>(*rsys, *test_library);
    object_material_instance->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
    object_material_instance->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
    object_material_instance->AssignTexture("base_tex", blank_color_red);
    auto floor_material_instance = std::make_shared<MaterialInstance>(*rsys, *test_library);
    floor_material_instance->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
    floor_material_instance->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
    floor_material_instance->AssignTexture("base_tex", blank_color_gray);

    // Prepare mesh
    auto floor_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    floor_go->GetTransformRef().SetScale({5.0f, 5.0f, 1.0f}).SetPosition({0.0f, 0.0f, 0.5f});
    auto floor_mesh_asset = std::make_shared<LowerPlaneMeshAsset>();
    auto floor_mesh_asset_ref = std::make_shared<AssetRef>(floor_mesh_asset);
    auto floor_mesh_comp = std::make_shared<MeshComponent>(floor_go);
    floor_mesh_comp->m_mesh_asset = floor_mesh_asset_ref;
    floor_mesh_comp->GetMaterials().resize(1);
    floor_mesh_comp->GetMaterials()[0] = floor_material_instance;
    floor_mesh_comp->RenderInit();
    assert(floor_mesh_comp->GetSubmesh(0)->GetVertexAttribute().HasAttribute(VertexAttributeSemantic::Texcoord0));

    auto cube_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    cube_go->GetTransformRef().SetScale({0.5f, 0.5f, 0.5f});
    auto cube_mesh_comp = std::make_shared<ShadowMapMeshComponent>(
        cube_go,
        std::filesystem::path{std::string(ENGINE_ASSETS_DIR) + "/meshes/cube.obj"},
        object_material_instance
    );

    auto shpere_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    shpere_go->GetTransformRef().SetScale({0.5f, 0.5f, 0.5f}).SetPosition({1.0f, 2.0f, 0.0f});
    auto sphere_mesh_comp = std::make_shared<ShadowMapMeshComponent>(
        shpere_go,
        std::filesystem::path{std::string(ENGINE_ASSETS_DIR) + "/meshes/sphere.obj"},
        object_material_instance
    );
    // We cannot call `RenderInit()` because this component has no associated asset.
    assert(cube_mesh_comp->GetSubmesh(0)->GetVertexAttribute().HasAttribute(VertexAttributeSemantic::Texcoord0));
    rsys->GetRendererManager().RegisterRendererComponent(cube_mesh_comp);
    assert(sphere_mesh_comp->GetSubmesh(0)->GetVertexAttribute().HasAttribute(VertexAttributeSemantic::Texcoord0));
    rsys->GetRendererManager().RegisterRendererComponent(sphere_mesh_comp);
    

    // Build Render Graph
    RenderGraphBuilder rgb{*rsys};
    rgb.RegisterImageAccess(*color);
    rgb.RegisterImageAccess(*depth);
    rgb.RegisterImageAccess(*shadow);

    using IAT = AccessHelper::ImageAccessType;
    rgb.UseImage(*shadow, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPassWithoutRT(
        [rsys, shadow, test_library, object_material_instance](GraphicsCommandBuffer &gcb) {
            vk::Extent2D shadow_map_extent{2048, 2048};
            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
            gcb.BeginRendering(
                {nullptr},
                {
                    shadow.get(), 
                    nullptr, 
                    AttachmentUtils::LoadOperation::Clear,
                    AttachmentUtils::StoreOperation::Store,
                    AttachmentUtils::DepthClearValue{1.0f, 0U}
                },
                shadow_map_extent,
                "Shadowmap Pass"
            );
            gcb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
            gcb.DrawRenderers(
                "Shadowmap",
                rsys->GetRendererManager().FilterAndSortRenderers({}),
                0,
                vk::Extent2D{shadow->GetTextureDescription().width, shadow->GetTextureDescription().height}
            );
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
        [rsys, object_material_instance, test_library](GraphicsCommandBuffer &gcb) {
            vk::Extent2D extent{rsys->GetSwapchain().GetExtent()};
            vk::Rect2D scissor{{0, 0}, extent};
            gcb.SetupViewport(extent.width, extent.height, scissor);
            gcb.DrawRenderers(
                "Lit",
                rsys->GetRendererManager().FilterAndSortRenderers({})
            );
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

        auto index = rsys->StartFrame();
        assert(index < 3);

        rg->Execute();
        rsys->CompleteFrame(*color, color->GetTextureDescription().width, color->GetTextureDescription().height);

        SDL_Delay(10);

        if (quited || frame_count > max_frame_count) break;
    }

    rsys->WaitForIdle();

    return 0;
}
