#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Mesh/PlaneMeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Core/Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/ObjTestMeshComponent.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "Framework/object/GameObject.h"
#include "Framework/world/Scene.h"
#include "Framework/world/WorldSystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include "UserInterface/GUISystem.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

struct LowerPlaneMeshAsset : public PlaneMeshAsset {
    constexpr static std::array<float, 12> REPLACEMENT_POSITION = {
        1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 0.5f, -1.0f, 1.0f, 0.5f, -1.0f, -1.0f, 0.5f
    };

    LowerPlaneMeshAsset() : PlaneMeshAsset() {
        assert(this->m_submeshes[0].positions.buffer_size == REPLACEMENT_POSITION.size() * sizeof(float));
        std::memcpy(
            reinterpret_cast<std::byte *>(m_submeshes[0].m_vertex_attributes.data())
                + this->m_submeshes[0].positions.buffer_offset,
            reinterpret_cast<const std::byte *>(REPLACEMENT_POSITION.data()),
            this->m_submeshes[0].positions.buffer_size
        );
        float *nb{
            reinterpret_cast<float *>(m_submeshes[0].m_vertex_attributes.data() + m_submeshes[0].normal.buffer_offset)
        };
        float *ne{reinterpret_cast<float *>(
            m_submeshes[0].m_vertex_attributes.data() + m_submeshes[0].normal.buffer_offset
            + m_submeshes[0].normal.buffer_size
        )};
        for (float *i = nb; i < ne; i += 3) {
            *(i + 2) = -1.0f;
        };
    }
};

class ShadowMapMeshComponent : public ObjTestMeshComponent {
public:
    ShadowMapMeshComponent(const GameObject &parent) : ObjTestMeshComponent(parent) {
    }

    void LoadData(std::filesystem::path mesh_file_name) {
        this->LoadMesh(mesh_file_name);
    }
};

std::array<MaterialTemplateAsset *, 2> ConstructMaterialTemplate() {
    auto am = MainClass::GetInstance()->GetAssetManager();
    std::array<MaterialTemplateAsset *, 2> templates{
        am->CreateAsset<MaterialTemplateAsset>(), am->CreateAsset<MaterialTemplateAsset>()
    };

    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());

    auto shadow_map_vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/shadowmap.vert.asset"});
    auto vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/blinn_phong.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef({*adb, "~/shaders/blinn_phong.frag.asset"});

    templates[0]->name = "Blinn-Phong Lit";
    templates[1]->name = "Shadow map pass";

    MaterialTemplateSinglePassProperties shadow_map_pass{}, lit_pass{};
    shadow_map_pass.shaders.shaders = std::vector{shadow_map_vs_ref};
    shadow_map_pass.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    shadow_map_pass.rasterizer.depth_bias_constant = 0.05f;
    shadow_map_pass.rasterizer.depth_bias_slope = 1.0f;

    lit_pass.shaders.shaders = std::vector{vs_ref, fs_ref};
    lit_pass.attachments.color = std::vector{ImageUtils::ImageFormat::R8G8B8A8UNorm};
    lit_pass.attachments.color_blending = std::vector{PipelineProperties::ColorBlendingProperties{}};
    lit_pass.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;

    templates[0]->properties = lit_pass;
    templates[1]->properties = shadow_map_pass;

    return templates;
}

MaterialLibraryAsset *ConstructMaterialLibrary(std::array<MaterialTemplateAsset *, 2> &templates) {
    auto am = MainClass::GetInstance()->GetAssetManager();
    auto *lib = am->CreateAsset<MaterialLibraryAsset>();
    lib->m_name = "Blinn-Phong w. Shadowmap";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = AssetRef(templates[0]);
    lib->material_bundle["Lit"] = ref;

    ref.expected_mesh_type = 0;
    ref.material_template = AssetRef(templates[1]);
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
    auto am = cmc->GetAssetManager();

    Transform transform{};
    transform.SetPosition({0.0f, 0.0f, -5.0f}).SetRotationEuler(glm::vec3{M_PI_2, 0.0f, 0.0f});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(1920.0 / 1080.0);
    camera->m_clipping_far = 1e2;
    camera->UpdateViewMatrix(transform);
    rsys->GetCameraManager().RegisterCamera(camera);
    rsys->GetCameraManager().SetActiveCameraIndex(camera->m_display_id);
    rsys->GetSceneDataManager().SetLightDirectional(0, glm::vec3{1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, 1.0f, 1.0f});
    rsys->GetSceneDataManager().SetLightCount(1);

    auto idesc = ImageTexture::ImageTextureDesc{
        .dimensions = 2,
        .width = 16,
        .height = 16,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
        .is_cube_map = false
    };
    std::shared_ptr blank_color_red =
        Engine::ImageTexture::CreateUnique(*rsys, idesc, Texture::SamplerDesc{}, "Blank color red");
    std::shared_ptr blank_color_gray =
        Engine::ImageTexture::CreateUnique(*rsys, idesc, Texture::SamplerDesc{}, "Blank color gray");
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_red, {1.0f, 0.0f, 0.0f, 0.0f});
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_gray, {0.5f, 0.5f, 0.5f, 0.0f});

    auto test_template_assets = ConstructMaterialTemplate();
    auto test_library_asset = ConstructMaterialLibrary(test_template_assets);
    auto test_library_asset_ref = AssetRef(test_library_asset);

    auto &scene = cmc->GetWorldSystem()->GetMainSceneRef();

    auto floor_mat_asset = am->CreateAsset<MaterialAsset>();
    floor_mat_asset->m_library = test_library_asset_ref;
    auto floor_mat_asset_ref = AssetRef(floor_mat_asset);

    auto &floor_go = scene.CreateGameObject();
    floor_go.GetTransformRef().SetScale({5.0f, 5.0f, 1.0f}).SetPosition({0.0f, 0.0f, 0.5f});
    auto floor_mesh_asset = am->CreateAsset<LowerPlaneMeshAsset>();
    auto floor_mesh_asset_ref = AssetRef(floor_mesh_asset);
    auto &floor_mesh_comp = scene.CreateComponent<StaticMeshComponent>(floor_go);
    floor_mesh_comp.m_mesh_asset = floor_mesh_asset_ref;
    floor_mesh_comp.m_material_assets.resize(1);
    floor_mesh_comp.m_material_assets[0] = floor_mat_asset_ref;
    floor_mesh_comp.Awake();
    {
        auto floor_handle = rsys->GetRenderResourceManager().Acquire<MaterialInstance>(floor_mat_asset_ref.GetGUID());
        auto floor_inst = rsys->GetRenderResourceManager().Resolve<MaterialInstance>(floor_handle);
        floor_inst->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
        floor_inst->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
        floor_inst->AssignTexture("base_tex", blank_color_gray);
        rsys->GetRenderResourceManager().Release(floor_handle);
    }

    auto cube_mat_asset = am->CreateAsset<MaterialAsset>();
    cube_mat_asset->m_library = test_library_asset_ref;
    auto cube_mat_asset_ref = AssetRef(cube_mat_asset);

    auto &cube_go = scene.CreateGameObject();
    cube_go.GetTransformRef().SetScale({0.5f, 0.5f, 0.5f});
    auto &cube_mesh_comp = scene.CreateComponent<ShadowMapMeshComponent>(cube_go);
    cube_mesh_comp.LoadData(std::filesystem::path{std::string(ENGINE_ASSETS_DIR) + "/meshes/cube.obj"});
    cube_mesh_comp.m_material_assets.resize(1);
    cube_mesh_comp.m_material_assets[0] = cube_mat_asset_ref;
    cube_mesh_comp.Awake();
    {
        auto cube_handle = rsys->GetRenderResourceManager().Acquire<MaterialInstance>(cube_mat_asset_ref.GetGUID());
        auto cube_inst = rsys->GetRenderResourceManager().Resolve<MaterialInstance>(cube_handle);
        cube_inst->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
        cube_inst->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
        cube_inst->AssignTexture("base_tex", blank_color_red);
        rsys->GetRenderResourceManager().Release(cube_handle);
    }

    auto sphere_mat_asset = am->CreateAsset<MaterialAsset>();
    sphere_mat_asset->m_library = test_library_asset_ref;
    auto sphere_mat_asset_ref = AssetRef(sphere_mat_asset);

    auto &sphere_go = scene.CreateGameObject();
    sphere_go.GetTransformRef().SetScale({0.5f, 0.5f, 0.5f}).SetPosition({1.0f, 2.0f, 0.0f});
    auto &sphere_mesh_comp = scene.CreateComponent<ShadowMapMeshComponent>(sphere_go);
    sphere_mesh_comp.LoadData(std::filesystem::path{std::string(ENGINE_ASSETS_DIR) + "/meshes/sphere.obj"});
    sphere_mesh_comp.m_material_assets.resize(1);
    sphere_mesh_comp.m_material_assets[0] = sphere_mat_asset_ref;
    sphere_mesh_comp.Awake();
    {
        auto sphere_handle = rsys->GetRenderResourceManager().Acquire<MaterialInstance>(sphere_mat_asset_ref.GetGUID());
        auto sphere_inst = rsys->GetRenderResourceManager().Resolve<MaterialInstance>(sphere_handle);
        sphere_inst->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
        sphere_inst->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
        sphere_inst->AssignTexture("base_tex", blank_color_red);
        rsys->GetRenderResourceManager().Release(sphere_handle);
    }

    RenderGraphBuilder2 rgb{*rsys};
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
    auto c = rgb.RequestRenderTargetTexture(desc, {});
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    auto d = rgb.RequestRenderTargetTexture(desc, {});
    desc.width = desc.height = 2048;
    auto s = rgb.RequestRenderTargetTexture(desc, {});

    using IAT = MemoryAccessTypeImageBits;

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Shadow Pass")
            .SetDepthStencilAttachment(
                {s,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}}
            )
            .SetRasterizerPassFunction([rsys, s](GraphicsCommandBuffer &gcb, const RenderGraph2 &rg) {
                vk::Extent2D shadow_map_extent{2048, 2048};
                vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
                auto sm = rg.GetInternalTextureResource(s);
                gcb.BeginRendering(
                    {nullptr},
                    {sm,
                     Engine::TextureSubresourceRange::GetSingleRange(),
                     AttachmentUtils::LoadOperation::Clear,
                     AttachmentUtils::StoreOperation::Store,
                     AttachmentUtils::DepthClearValue{1.0f, 0U}},
                    shadow_map_extent,
                    "Shadowmap Pass"
                );
                gcb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
                gcb.DrawRenderers(
                    "Shadowmap",
                    rsys->GetRendererManager().FilterAndSortRenderers({}),
                    0,
                    vk::Extent2D{sm->GetTextureDescription().width, sm->GetTextureDescription().height}
                );
                gcb.EndRendering();
            })
            .Get()
    );

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Lit Pass")
            .AppendColorAttachment(
                {c, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
            )
            .SetDepthStencilAttachment(
                {d,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::DontCare,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}}
            )
            .UseImage(s, IAT::ShaderSampledRead)
            .SetRasterizerPassFunction([rsys](GraphicsCommandBuffer &gcb, const RenderGraph2 &) {
                vk::Extent2D extent{rsys->GetSwapchain().GetExtent()};
                vk::Rect2D scissor{{0, 0}, extent};
                gcb.SetupViewport(extent.width, extent.height, scissor);
                gcb.DrawRenderers("Lit", rsys->GetRendererManager().FilterAndSortRenderers({}));
            })
            .WrapRenderPass()
            .Get()
    );

    auto rg{rgb.BuildRenderGraph()};
    auto sm = rg.GetInternalTextureResource(s);
    rsys->GetSceneDataManager().SetLightShadowMap(0, *sm);

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

        floor_mesh_comp.PreRenderUpdate();
        cube_mesh_comp.PreRenderUpdate();
        sphere_mesh_comp.PreRenderUpdate();

        rg.Execute(*rsys);
        auto color = rg.GetInternalTextureResource(c);
        rsys->CompleteFrame(*color, color->GetTextureDescription().width, color->GetTextureDescription().height);

        SDL_Delay(10);

        if (quited || frame_count > max_frame_count) break;
    }

    rsys->WaitForIdle();

    return 0;
}
