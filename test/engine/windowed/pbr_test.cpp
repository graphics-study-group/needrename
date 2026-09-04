#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>
#include <numbers>

#include "Render/FullRenderSystem.h"

#include "Asset/AssetManager/AssetManager.h"
#include "Core/Functional/SDLWindow.h"
#include "Core/guid.h"
#include "Framework/Component/RenderComponent/ObjTestMeshComponent.h"
#include "Framework/Import/ObjLoader.h"
#include "Framework/MainClass.h"
#include "Framework/Object/GameObject.h"
#include "Framework/World/Scene.h"
#include "Framework/World/WorldSystem.h"
#include "Render/Asset/Material/MaterialAsset.h"
#include "Render/Asset/Material/MaterialTemplateAsset.h"
#include "Render/Asset/Mesh/MeshAsset.h"
#include "Render/Asset/Texture/Image2DTextureAsset.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>

#include "Render/UserInterface/GUISystem.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

std::pair<MaterialLibraryAsset *, MaterialTemplateAsset *> ConstructMaterial() {
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    auto am = MainClass::GetInstance()->GetAssetManager();
    auto test_asset = am->CreateAsset<MaterialTemplateAsset>();
    auto lib_asset = am->CreateAsset<MaterialLibraryAsset>();
    auto vs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/pbr_base.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/lambertian_cook_torrance.frag.asset"});

    test_asset->name = "LambertianCookTorrancePBR";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {Rhi::ImageFormat::R11G11B10UFloat};
    mtspp.attachments.color_blending = {PipelineProperties::ColorBlendingProperties{}};
    mtspp.attachments.depth = Rhi::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<AssetRef>{vs_ref, fs_ref};
    test_asset->properties = mtspp;

    lib_asset->m_name = "LambertianCookTorrancePBR";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = AssetRef(test_asset);
    lib_asset->material_bundle[""] = ref;

    return std::make_pair(lib_asset, test_asset);
}

class PBRMeshComponent : public ObjTestMeshComponent {
    Transform transform;
    struct UniformData {
        float metalness;
        float roughness;
        glm::vec4 emissive;
        glm::vec4 albedo;
    };
    UniformData m_uniform_data{1.0, 1.0, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}, glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}};
    std::vector<GUID> m_material_guids{};

public:
    PBRMeshComponent(const GameObject &parentObject) : ObjTestMeshComponent(parentObject), transform() {
    }

    void LoadData(
        std::filesystem::path mesh_file_name,
        AssetRef lib_asset_ref,
        std::shared_ptr<Rhi::Texture> albedo,
        std::shared_ptr<Rhi::Texture> MRAO,
        std::shared_ptr<Rhi::Texture> normal,
        std::shared_ptr<Rhi::Texture> emissive
    ) {
        this->LoadMesh(mesh_file_name);

        auto am = MainClass::GetInstance()->GetAssetManager();
        auto rsys = MainClass::GetInstance()->GetRenderSystem();
        auto *mi_mng = rsys->GetRenderResourceManager<RenderSystemState::MaterialInstanceManager>();
        auto masset = m_mesh_asset.as<MeshAsset>();
        this->m_material_assets.clear();
        for (size_t i = 0; i < masset->GetSubmeshCount(); i++) {
            this->m_material_assets.push_back(AssetRef(am->CreateAsset<MaterialAsset>()->GetGUID()));
            this->m_material_assets.back().as<MaterialAsset>()->m_library = lib_asset_ref;
            auto handle = mi_mng->CreateOrReuseFromAsset(this->m_material_assets.back().GetGUID());
            auto ptr = mi_mng->Resolve(handle);
            ptr->AssignTexture("albedoSampler", albedo);
            ptr->AssignTexture("MRAOSampler", MRAO);
            ptr->AssignTexture("normalSampler", normal);
            ptr->AssignTexture("emissiveSampler", emissive);
            m_material_guids.push_back(this->m_material_assets.back().GetGUID());
        }
    }

    ~PBRMeshComponent() = default;

    Transform GetWorldTransform() const override {
        return transform;
    }

    void UpdateUniformData(float metalness, float roughness, glm::vec4 emissive, glm::vec4 albedo) {
        uint8_t identity = (fabs(metalness - m_uniform_data.metalness) < 1e-3)
                           + (fabs(roughness - m_uniform_data.roughness) < 1e-3)
                           + (glm::length(emissive - m_uniform_data.emissive) < 1e-3)
                           + (glm::length(albedo - m_uniform_data.albedo) < 1e-3);
        if (identity == 4) return;
        m_uniform_data = {.metalness = metalness, .roughness = roughness, .emissive = emissive, .albedo = albedo};

        auto *rsys = MainClass::GetInstance()->GetRenderSystem().get();
        auto *mat_mng = rsys->GetRenderResourceManager<RenderSystemState::MaterialInstanceManager>();
        for (auto guid : m_material_guids) {
            auto handle = mat_mng->CreateOrReuseFromAsset(guid);
            auto *inst = mat_mng->Resolve(handle);
            inst->AssignScalarVariable("Material::metalnessFactor", metalness);
            inst->AssignScalarVariable("Material::roughnessFactor", roughness);
            inst->AssignVectorVariable("Material::emissiveFactor", emissive);
            inst->AssignVectorVariable("Material::albedoFactor", albedo);
        }
    }
};

struct {
    float zenith, azimuth;
    float metalness, roughness;
    glm::vec4 emissive;
    glm::vec4 albedo;
} g_SceneData{
    std::numbers::pi_v<float> / 2.0f,
    std::numbers::pi_v<float> / 2.0f * 3,
    0.5f,
    0.5f,
    glm::vec4{0.0f, 0.0f, 0.0f, 1.0f},
    glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}
};

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
    ImGui::ColorEdit3("Albedo", &g_SceneData.albedo[0]);
    ImGui::ColorEdit3("Emissive", &g_SceneData.emissive[0]);
    ImGui::End();
}

void SubmitSceneData(std::shared_ptr<RenderSystem> rsys) {
    rsys->GetSceneDataManager().SetLightDirectionalNonShadowCasting(
        0, GetCartesian(g_SceneData.zenith, g_SceneData.azimuth), glm::vec3{2.0, 2.0, 2.0}
    );
    rsys->GetSceneDataManager().SetLightCountNonShadowCasting(1);
}

void SubmitMaterialData(PBRMeshComponent *mesh) {
    mesh->UpdateUniformData(g_SceneData.metalness, g_SceneData.roughness, g_SceneData.emissive, g_SceneData.albedo);
}

int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_VIDEO);

    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    int displayIndex = 1;
    auto displayMode = SDL_GetDesktopDisplayMode(displayIndex);
    if (displayMode == nullptr) {
        SDL_Log("Failed to get display mode: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    int screenWidth = displayMode->w;
    int screenHeight = displayMode->h;
    SDL_Log("Screen Resolution: %dx%d @ %fHz", screenWidth, screenHeight, displayMode->refresh_rate);

    StartupOptions opt{.resol_x = (int)(screenWidth * 0.9), .resol_y = (int)(screenHeight * 0.9), .title = "PBR Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto asys = cmc->GetAssetManager();
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();
    auto pbr_material_assets = ConstructMaterial();
    auto pbr_material_asset_ref = AssetRef(pbr_material_assets.first);
    auto pbr_material = std::make_shared<MaterialLibrary>(*rsys);
    pbr_material->Instantiate(*pbr_material_assets.first);

    auto gsys = cmc->GetGUISystem();
    gsys->CreateVulkanBackend(*rsys, Rhi::GetVkFormat(Engine::Rhi::ImageFormat::R8G8B8A8UNorm));

    RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = (uint32_t)screenWidth,
        .height = (uint32_t)screenHeight,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat,
        .multisample = 1,
        .is_cube_map = false
    };
    std::shared_ptr hdr_color{RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "HDR Color Attachment"
    )};
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    std::shared_ptr color{RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Attachment"
    )};
    desc.mipmap_levels = 1;
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    std::shared_ptr depth{RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Depth Attachment"
    )};

    Rhi::ImageTexture::ImageTextureDesc empty_desc{
        .dimensions = 2,
        .width = 4,
        .height = 4,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = Rhi::ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
        .is_cube_map = false
    };
    std::shared_ptr red_texture = Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(), empty_desc, Rhi::Texture::SamplerDesc{}, "Sampled Albedo"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*red_texture, {1.0, 0.0, 0.0, 1.0});
    std::shared_ptr MRAO_texture = Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(), empty_desc, Rhi::Texture::SamplerDesc{}, "Sampled MRAO"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*MRAO_texture, {1.0, 1.0, 1.0, 1.0});
    std::shared_ptr normal_texture = Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(), empty_desc, Rhi::Texture::SamplerDesc{}, "Sampled Normal"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*normal_texture, {0.5, 0.5, 1.0, 1.0});
    std::shared_ptr emissive_texture = Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(), empty_desc, Rhi::Texture::SamplerDesc{}, "Sampled Emissive"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*emissive_texture, {1.0, 1.0, 1.0, 1.0});

    auto &scene = cmc->GetWorldSystem()->GetMainSceneRef();
    // Setup mesh
    std::filesystem::path mesh_path{std::string(ENGINE_ASSETS_DIR) + "/meshes/sphere.obj"};
    auto &go = scene.CreateGameObject();
    auto tmc = &scene.CreateComponent<PBRMeshComponent>(go);
    tmc->LoadData(mesh_path, pbr_material_asset_ref, red_texture, MRAO_texture, normal_texture, emissive_texture);
    tmc->Awake();

    // Setup camera
    Transform transform{};
    transform.SetPosition({0.0f, 5.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio((double)screenWidth / screenHeight);
    camera->UpdateViewMatrix(transform);
    rsys->GetCameraManager().RegisterCamera(camera);
    rsys->GetCameraManager().SetActiveCameraIndex(camera->m_display_id);

    // Setup compute shader
    auto cs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/bloom.comp.asset"});
    auto bloom_compute_stage = std::make_shared<Rhi::ComputeStage>(rsys->GetDeviceContext());
    bloom_compute_stage->Instantiate(cs_ref.as<ShaderAsset>()->binary, cs_ref.as<ShaderAsset>()->m_name);
    auto &bloom_compute_binding =
        bloom_compute_stage->AllocateResourceBinding(RenderSystemState::FrameManager::FRAMES_IN_FLIGHT);

    // Build render graph.
    RenderGraphBuilder rgb{*rsys};
    RenderTargetTexture::RenderTargetTextureDesc rtt_desc{
        .dimensions = 2,
        .width = (uint32_t)screenWidth,
        .height = (uint32_t)screenHeight,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat,
        .multisample = 1,
        .is_cube_map = false
    };
    auto hc = rgb.RequestResizableRenderTargetTexture(rtt_desc, Rhi::Texture::SamplerDesc{});
    rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    auto d = rgb.RequestResizableRenderTargetTexture(rtt_desc, Rhi::Texture::SamplerDesc{});
    rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    auto c = rgb.RequestResizableRenderTargetTexture(rtt_desc, Rhi::Texture::SamplerDesc{});
    // Color pass
    using IAT = Rhi::MemoryAccessTypeImageBits;
    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Color Pass")
            .AppendColorAttachment(
                {hc, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
            )
            .SetDepthStencilAttachment(
                {d,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::DontCare,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}}
            )
            .SetPassFunction([rsys](CommandBuffer &cb, const RenderGraph &) {
                cb.DrawRenderers("", rsys->GetRendererManager().FilterAndSortRenderers({}));
            })
            .WrapRenderPass()
            .Get()
    );

    // Bloom pass
    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Bloom Fx Pass")
            .UseImage(hc, IAT::ShaderRandomRead)
            .UseImage(c, IAT::ShaderRandomWrite)
            .SetAffinity(RenderGraphPassAffinity::Compute)
            .SetPassFunction([bloom_compute_stage, &bloom_compute_binding, hc, c, screenWidth, screenHeight](
                                 CommandBuffer &cb, const RenderGraph &rg
                             ) {
                // These descriptors should be cached, so there should be only one write.
                bloom_compute_binding.GetShaderResourceBinding().BindTexture(
                    "inputImage", *rg.GetInternalTextureResource(hc)
                );
                bloom_compute_binding.GetShaderResourceBinding().BindTexture(
                    "outputImage", *rg.GetInternalTextureResource(c)
                );
                cb.BindComputeStage(*bloom_compute_stage);
                cb.BindComputeResource(bloom_compute_binding);
                cb.DispatchCompute(screenWidth / 16 + 1, screenHeight / 16 + 1, 1);
            })
            .Get()
    );

    // GUI pass
    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("GUI Pass")
            .AppendColorAttachment(
                {c, {}, AttachmentUtils::LoadOperation::Load, AttachmentUtils::StoreOperation::Store}
            )
            .SetPassFunction([rsys, gsys](CommandBuffer &cb, const RenderGraph &) {
                gsys->DrawGUI(cb.GetCommandBuffer());
            })
            .WrapRenderPass()
            .Get()
    );

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
        SubmitSceneData(rsys);
        SubmitMaterialData(tmc);
        tmc->PreRenderUpdate();

        // Draw
        auto index = rsys->StartFrame();
        if (index == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after retry — skip this frame.
            continue;
        }

        rsys->GetFrameManager().BeginMainCommandBuffer();
        rg->RecordIntoMainCommandBuffer(*rsys);
        auto color = rg->GetInternalTextureResource(c);
        rsys->CompleteFrame(*color, Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite);

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
