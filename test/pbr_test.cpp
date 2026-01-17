#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include <tiny_obj_loader.h>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Loader/ObjLoader.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Core/Functional/SDLWindow.h"
#include "UserInterface/GUISystem.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Framework/component/RenderComponent/ObjTestMeshComponent.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

std::pair<std::shared_ptr<MaterialLibraryAsset>, std::shared_ptr<MaterialTemplateAsset>> ConstructMaterial() {
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(
        MainClass::GetInstance()->GetAssetDatabase()
    );
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto lib_asset = std::make_shared<MaterialLibraryAsset>();
    auto vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/pbr_base.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef({*adb, "~/shaders/lambertian_cook_torrance.frag.asset"});
    assert(vs_ref && fs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);

    test_asset->name = "LambertianCookTorrancePBR";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {ImageUtils::ImageFormat::R11G11B10UFloat};
    mtspp.attachments.color_blending = {PipelineProperties::ColorBlendingProperties{}};
    mtspp.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<std::shared_ptr<AssetRef>>{vs_ref, fs_ref};
    test_asset->properties = mtspp;

    lib_asset->m_name = "LambertianCookTorrancePBR";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = std::make_shared<AssetRef>(test_asset);
    lib_asset->material_bundle[""] = ref;

    return std::make_pair(lib_asset, test_asset);
}

class PBRMeshComponent : public ObjTestMeshComponent {
    Transform transform;
    struct UniformData {
        float metalness;
        float roughness;
    };
    UniformData m_uniform_data{1.0, 1.0};

public:
    PBRMeshComponent(
        std::filesystem::path mesh_file_name,
        std::shared_ptr<MaterialLibrary> library,
        std::shared_ptr<const Texture> albedo
    ) : ObjTestMeshComponent(mesh_file_name), transform() {
        auto system = m_system.lock();
        auto &helper = system->GetFrameManager().GetSubmissionHelper();

        for (size_t i = 0; i < m_submeshes.size(); i++) {
            auto ptr = std::make_shared<MaterialInstance>(*system, *library);
            ptr->AssignTexture("albedoSampler", albedo);
            m_materials.push_back(ptr);
        }
    }

    ~PBRMeshComponent() {
        m_materials.clear();
        m_submeshes.clear();
    }

    Transform GetWorldTransform() const override {
        return transform;
    }

    void UpdateUniformData(float metalness, float roughness) {
        uint8_t identity =
            (fabs(metalness - m_uniform_data.metalness) < 1e-3) + (fabs(roughness - m_uniform_data.roughness) < 1e-3);
        if (identity == 2) return;
        m_uniform_data = {.metalness = metalness, .roughness = roughness};

        for (auto &material : m_materials) {
            material->AssignScalarVariable("Material::metalness", metalness);
            material->AssignScalarVariable("Material::roughness", roughness);
        }
    }
};

struct {
    float zenith, azimuth;
    float metalness, roughness;
} g_SceneData{M_PI_2, M_PI_2, 0.5f, 0.5f};

glm::vec3 GetCartesian(float zenith, float azimuth) {
    static constexpr float RADIUS = 2.0f;
    return glm::vec3{RADIUS * sin(zenith) * cos(azimuth), RADIUS * sin(zenith) * sin(azimuth), RADIUS * cos(zenith)};
}

void PrepareGui() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    ImGui::SetNextWindowPos({10, 10});
    ImGui::SetNextWindowSize(ImVec2{300, 300});
    ImGui::Begin("Configuration", nullptr, flags);
    ImGui::SliderAngle("Zenith", &g_SceneData.zenith, -180.0f, 180.0f);
    ImGui::SliderAngle("Azimuth", &g_SceneData.azimuth, 0.0f, 360.0f);

    glm::vec3 light_source = GetCartesian(g_SceneData.zenith, g_SceneData.azimuth);
    ImGui::Text("Coordinate: (%.3f, %.3f, %.3f).", light_source.x, light_source.y, light_source.z);

    ImGui::Separator();

    ImGui::SliderFloat("Metalness", &g_SceneData.metalness, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &g_SceneData.roughness, 0.0f, 1.0f);
    ImGui::End();
}

void SubmitSceneData(std::shared_ptr<RenderSystem> rsys, uint32_t id) {
    rsys->GetSceneDataManager().SetLightDirectionalNonShadowCasting(
        0, 
        GetCartesian(g_SceneData.zenith, g_SceneData.azimuth), 
        glm::vec3{2.0, 2.0, 2.0}
    );
    rsys->GetSceneDataManager().SetLightCountNonShadowCasting(1);
}

void SubmitMaterialData(std::shared_ptr<PBRMeshComponent> mesh) {
    mesh->UpdateUniformData(g_SceneData.metalness, g_SceneData.roughness);
}

int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_VIDEO);

    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "PBR Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto asys = cmc->GetAssetManager();
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    asys->LoadAssetsInQueue();

    auto rsys = cmc->GetRenderSystem();
    auto pbr_material_assets = ConstructMaterial();
    auto pbr_material_asset_ref = std::make_shared<AssetRef>(pbr_material_assets.first);
    auto pbr_material = std::make_shared<MaterialLibrary>(*rsys);
    pbr_material->Instantiate(*pbr_material_assets.first);

    auto gsys = cmc->GetGUISystem();
    gsys->CreateVulkanBackend(*rsys, ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm));

    RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat,
        .multisample = 1,
        .is_cube_map = false
    };
    std::shared_ptr hdr_color{RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "HDR Color Attachment")};
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    std::shared_ptr color{RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment")};
    desc.mipmap_levels = 1;
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    std::shared_ptr depth{RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Depth Attachment")};

    std::shared_ptr red_texture = ImageTexture::CreateUnique(
        *rsys, 
        ImageTexture::ImageTextureDesc{
            .dimensions = 2,
            .width = 4,
            .height = 4,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
            .is_cube_map = false
        }, 
        Texture::SamplerDesc{}, 
        "Sampled Albedo"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*red_texture, {1.0, 0.0, 0.0, 1.0});

    // Setup mesh
    std::filesystem::path mesh_path{std::string(ENGINE_ASSETS_DIR) + "/meshes/sphere.obj"};
    std::shared_ptr tmc = std::make_shared<PBRMeshComponent>(mesh_path, pbr_material, red_texture);
    rsys->GetRendererManager().RegisterRendererComponent(tmc);

    // Setup camera
    Transform transform{};
    transform.SetPosition({0.0f, 5.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(1920.0 / 1080.0);
    camera->UpdateViewMatrix(transform);
    rsys->GetCameraManager().RegisterCamera(camera);
    rsys->GetCameraManager().SetActiveCameraIndex(camera->m_display_id);

    // Setup compute shader
    auto cs_ref = adb->GetNewAssetRef({*adb, "~/shaders/bloom.comp.asset"});
    assert(cs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(cs_ref);
    auto bloom_compute_stage = std::make_shared<ComputeStage>(*rsys);
    bloom_compute_stage->Instantiate(*cs_ref->cas<ShaderAsset>());
    bloom_compute_stage->AssignTexture("inputImage", hdr_color);
    bloom_compute_stage->AssignTexture("outputImage", color);

    // Build render graph.
    RenderGraphBuilder rgb{*rsys};
    rgb.RegisterImageAccess(*hdr_color);
    rgb.RegisterImageAccess(*depth);
    rgb.RegisterImageAccess(*color);

    // Color pass
    using IAT = AccessHelper::ImageAccessType;
    rgb.UseImage(*hdr_color, IAT::ColorAttachmentWrite);
    rgb.UseImage(*depth, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPass(
        AttachmentUtils::AttachmentDescription{
            hdr_color.get(), nullptr, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store
        },
        AttachmentUtils::AttachmentDescription{
            depth.get(),
            nullptr,
            AttachmentUtils::LoadOperation::Clear,
            AttachmentUtils::StoreOperation::DontCare,
            AttachmentUtils::DepthClearValue{1.0f, 0U}
        },
        [rsys](GraphicsCommandBuffer &gcb) {
            gcb.DrawRenderers("", rsys->GetRendererManager().FilterAndSortRenderers({}));
        },
        "Color pass"
    );

    // Bloom pass
    rgb.UseImage(*hdr_color, IAT::ShaderReadRandomWrite);
    rgb.UseImage(*color, IAT::ShaderRandomWrite);
    rgb.RecordComputePass(
        [bloom_compute_stage, color](ComputeCommandBuffer &ccb) {
            ccb.BindComputeStage(*bloom_compute_stage);
            ccb.DispatchCompute(
                color->GetTextureDescription().width / 16 + 1, color->GetTextureDescription().height / 16 + 1, 1
            );
        },
        "Bloom FX pass"
    );

    // GUI pass
    rgb.UseImage(*color, IAT::ColorAttachmentWrite);
    rgb.RecordRasterizerPassWithoutRT([rsys, gsys, color](GraphicsCommandBuffer &gcb) {
        gsys->DrawGUI(
            {color.get(), nullptr, AttachmentUtils::LoadOperation::Load, AttachmentUtils::StoreOperation::Store},
            rsys->GetSwapchain().GetExtent(),
            gcb
        );
    });

    auto rg{rgb.BuildRenderGraph()};

    uint64_t frame_count = 0;
    uint64_t start_timer = SDL_GetPerformanceCounter();
    while (++frame_count) {
        bool quited = false;
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
            gsys->ProcessEvent(&event);
        }
        if (quited) break;

        gsys->PrepareGUI();

        // Draw GUI and gather data
        PrepareGui();

        // Submit data
        SubmitSceneData(rsys, rsys->GetFrameManager().GetFrameInFlight());
        SubmitMaterialData(tmc);

        // Draw
        auto index = rsys->StartFrame();

        rg.Execute();
        rsys->CompleteFrame(
            *color,
            color->GetTextureDescription().width,
            color->GetTextureDescription().height
        );

        // SDL_Delay(5);

        if ((int64_t)frame_count >= max_frame_count) break;
    }
    uint64_t end_timer = SDL_GetPerformanceCounter();
    uint64_t duration = end_timer - start_timer;
    double duration_time = 1.0 * duration / SDL_GetPerformanceFrequency();
    SDL_LogInfo(
        0,
        "Took %lf seconds for %llu frames (avg. %lf fps).",
        duration_time,
        frame_count,
        frame_count * 1.0 / duration_time
    );
    rsys->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    return 0;
}
