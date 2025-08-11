#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include <tiny_obj_loader.h>

#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Loader/ObjLoader.h"
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

std::shared_ptr<MaterialTemplateAsset> ConstructMaterialTemplate() {
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto vs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/pbr_base.vert.spv.asset");
    auto fs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef(
        "~/shaders/lambertian_cook_torrance.frag.spv.asset"
    );
    assert(vs_ref && fs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);

    test_asset->name = "LambertianCookTorrancePBR";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {ImageUtils::ImageFormat::R11G11B10UFloat};
    mtspp.attachments.color_blending = {PipelineProperties::ColorBlendingProperties{}};
    mtspp.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<std::shared_ptr<AssetRef>>{vs_ref, fs_ref};

    test_asset->properties.properties[0] = mtspp;

    return test_asset;
}

class MeshComponentFromFile : public MeshComponent {
    Transform transform;

    struct UniformData {
        float metalness;
        float roughness;
    };

    UniformData m_uniform_data{1.0, 1.0};

    void LoadMesh(std::filesystem::path mesh) {
        tinyobj::ObjReaderConfig reader_config{};
        tinyobj::ObjReader reader{};

        m_materials.clear();
        m_submeshes.clear();

        if (!reader.ParseFromFile(mesh.string(), reader_config)) {
            SDL_LogCritical(0, "Failed to load OBJ file %s", mesh.string().c_str());
            if (!reader.Error().empty()) {
                SDL_LogCritical(0, "TinyObjLoader reports: %s", reader.Error().c_str());
            }
            throw std::runtime_error("Cannot load OBJ file");
        }

        SDL_LogInfo(0, "Loaded OBJ file %s", mesh.string().c_str());
        if (!reader.Warning().empty()) {
            SDL_LogWarn(0, "TinyObjLoader reports: %s", reader.Warning().c_str());
        }

        const auto &attrib = reader.GetAttrib();
        const auto &origin_shapes = reader.GetShapes();
        std::vector<tinyobj::shape_t> shapes;
        const auto &origin_materials = reader.GetMaterials();

        // We dont need materials from the obj file.
        std::vector<tinyobj::material_t> materials;

        // Split the subshapes by material
        for (size_t shp = 0; shp < origin_shapes.size(); shp++) {
            const auto &shape = origin_shapes[shp];
            auto shape_vertices_size = shape.mesh.num_face_vertices.size();
            std::map<int, tinyobj::shape_t> material_id_map;
            int shape_id = 0;
            for (size_t fc = 0; fc < shape_vertices_size; fc++) {
                auto &material_id = shape.mesh.material_ids[fc];
                if (material_id_map.find(material_id) == material_id_map.end()) {
                    material_id_map[material_id] = tinyobj::shape_t{
                        .name = shape.name + "_" + std::to_string(shape_id++),
                        .mesh = tinyobj::mesh_t{},
                        .lines = tinyobj::lines_t{},
                        .points = tinyobj::points_t{}
                    };
                }
                auto &new_shape = material_id_map[material_id];
                unsigned int face_vertex_count = shape.mesh.num_face_vertices[fc];
                assert(face_vertex_count == 3);
                new_shape.mesh.num_face_vertices.push_back(face_vertex_count);
                new_shape.mesh.material_ids.push_back(material_id);
                new_shape.mesh.smoothing_group_ids.push_back(shape.mesh.smoothing_group_ids[fc]);
                for (unsigned int vrtx = 0; vrtx < face_vertex_count; vrtx++) {
                    new_shape.mesh.indices.push_back(shape.mesh.indices[fc * 3 + vrtx]);
                }
            }

            for (const auto &[_, new_shape] : material_id_map) {
                shapes.push_back(new_shape);
                materials.push_back(tinyobj::material_t{});
                std::fill(
                    shapes.back().mesh.material_ids.begin(), shapes.back().mesh.material_ids.end(), materials.size() - 1
                );
            }
        }

        this->m_mesh_asset =
            std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(std::make_shared<MeshAsset>()));
        ObjLoader loader;
        loader.LoadMeshAssetFromTinyObj(*(this->m_mesh_asset->as<MeshAsset>()), attrib, shapes);

        assert(m_mesh_asset && m_mesh_asset->IsValid());
        m_submeshes.clear();
        size_t submesh_count = m_mesh_asset->as<MeshAsset>()->GetSubmeshCount();
        for (size_t i = 0; i < submesh_count; i++) {
            m_submeshes.push_back(std::make_shared<HomogeneousMesh>(m_system, m_mesh_asset, i));
        }
    }

public:
    MeshComponentFromFile(
        std::filesystem::path mesh_file_name,
        std::shared_ptr<MaterialTemplate> material_template,
        std::shared_ptr<const SampledTexture> albedo
    ) : MeshComponent(std::weak_ptr<GameObject>()), transform() {
        LoadMesh(mesh_file_name);

        auto system = m_system.lock();
        auto &helper = system->GetFrameManager().GetSubmissionHelper();

        auto id_albedo = material_template->GetVariableIndex("albedoSampler", 0).value();
        assert(id_albedo.second == false);
        for (size_t i = 0; i < m_submeshes.size(); i++) {
            auto ptr = std::make_shared<MaterialInstance>(*system, material_template);
            ptr->WriteTextureUniform(0, 1, albedo);
            m_materials.push_back(ptr);
        }
    }

    ~MeshComponentFromFile() {
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

        auto id_metalness = m_materials[0]->GetTemplate().GetVariableIndex("metalness", 0).value();
        auto id_roughness = m_materials[0]->GetTemplate().GetVariableIndex("roughness", 0).value();
        assert(id_metalness.second == true && id_roughness.second == true);
        for (auto &material : m_materials) {
            material->WriteUBOUniform(0, id_metalness.first, metalness);
            material->WriteUBOUniform(0, id_roughness.first, roughness);
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
    ConstantData::PerSceneStruct scene{
        1,
        {glm::vec4{GetCartesian(g_SceneData.zenith, g_SceneData.azimuth), 0.0f}},
        {glm::vec4{2.0, 2.0, 2.0, 0.0}},
    };
    auto ptr = rsys->GetGlobalConstantDescriptorPool().GetPerSceneConstantMemory(id);
    memcpy(ptr, &scene, sizeof scene);
    rsys->GetGlobalConstantDescriptorPool().FlushPerSceneConstantMemory(id);
}

void SubmitMaterialData(std::shared_ptr<MeshComponentFromFile> mesh) {
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
    asys->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    asys->LoadBuiltinAssets();
    asys->LoadAssetsInQueue();

    auto rsys = cmc->GetRenderSystem();
    auto pbr_material_template_asset = ConstructMaterialTemplate();
    auto pbr_material_template_asset_ref = std::make_shared<AssetRef>(pbr_material_template_asset);
    auto pbr_material_template = std::make_shared<MaterialTemplate>(*rsys);
    pbr_material_template->Instantiate(*pbr_material_template_asset_ref->cas<MaterialTemplateAsset>());

    auto gsys = cmc->GetGUISystem();
    gsys->CreateVulkanBackend(ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm));

    std::shared_ptr hdr_color{std::make_shared<Texture>(*rsys)};
    std::shared_ptr color{std::make_shared<Texture>(*rsys)};
    std::shared_ptr depth{std::make_shared<Texture>(*rsys)};
    Engine::Texture::TextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .format = Engine::ImageUtils::ImageFormat::R11G11B10UFloat,
        .type = Engine::ImageUtils::ImageType::ColorGeneral,
        .mipmap_levels = 1,
        .array_layers = 1,
        .is_cube_map = false
    };
    hdr_color->CreateTexture(desc, "HDR Color Attachment");

    desc.format = Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm;
    color->CreateTexture(desc, "Color Attachment");

    desc.mipmap_levels = 1;
    desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
    desc.type = Engine::ImageUtils::ImageType::DepthImage;
    depth->CreateTexture(desc, "Depth Attachment");

    auto red_texture = std::make_shared<SampledTexture>(*rsys);
    desc = {
        .dimensions = 2,
        .width = 4,
        .height = 4,
        .depth = 1,
        .format = Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB,
        .type = Engine::ImageUtils::ImageType::TextureImage,
        .mipmap_levels = 1,
        .array_layers = 1,
        .is_cube_map = false
    };
    red_texture->CreateTextureAndSampler(desc, {}, "Sampled Albedo");
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*red_texture, {1.0, 0.0, 0.0, 1.0});

    Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
    color_att.texture = hdr_color.get();
    color_att.load_op = AttachmentUtils::LoadOperation::Clear;
    color_att.store_op = AttachmentUtils::StoreOperation::Store;
    depth_att.texture = depth.get();
    depth_att.load_op = AttachmentUtils::LoadOperation::Clear;
    depth_att.store_op = AttachmentUtils::StoreOperation::DontCare;

    auto cs_ref = MainClass::GetInstance()->GetAssetManager()->GetNewAssetRef("~/shaders/bloom.comp.spv.asset");
    assert(cs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(cs_ref);
    auto bloom_compute_stage = std::make_shared<ComputeStage>(*rsys);
    bloom_compute_stage->Instantiate(*cs_ref->cas<ShaderAsset>());
    bloom_compute_stage->SetDescVariable(
        bloom_compute_stage->GetVariableIndex("inputImage").value().first,
        std::const_pointer_cast<const Texture>(hdr_color)
    );
    bloom_compute_stage->SetDescVariable(
        bloom_compute_stage->GetVariableIndex("outputImage").value().first,
        std::const_pointer_cast<const Texture>(color)
    );

    // Setup mesh
    std::filesystem::path mesh_path{std::string(ENGINE_ASSETS_DIR) + "/sphere/sphere.obj"};
    std::shared_ptr tmc = std::make_shared<MeshComponentFromFile>(mesh_path, pbr_material_template, red_texture);
    rsys->GetRendererManager().RegisterRendererComponent(tmc);

    // Setup camera
    Transform transform{};
    transform.SetPosition({0.0f, 5.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(1920.0 / 1080.0);
    rsys->SetActiveCamera(camera);

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
        auto context = rsys->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer &cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());

        cb.Begin();
        context.UseImage(
            *hdr_color,
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.UseImage(
            *depth,
            GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.PrepareCommandBuffer();
        vk::Extent2D extent{rsys->GetSwapchain().GetExtent()};
        cb.BeginRendering(color_att, depth_att, extent);
        cb.DrawRenderers(rsys->GetRendererManager().FilterAndSortRenderers({}), 0);
        cb.EndRendering();

        auto cctx = rsys->GetFrameManager().GetComputeContext();
        auto ccb = dynamic_cast<ComputeCommandBuffer &>(cctx.GetCommandBuffer());
        cctx.UseImage(
            *hdr_color,
            ComputeContext::ImageComputeAccessType::ShaderReadRandomWrite,
            ComputeContext::ImageAccessType::ColorAttachmentWrite
        );
        cctx.UseImage(
            *color, ComputeContext::ImageComputeAccessType::ShaderRandomWrite, ComputeContext::ImageAccessType::None
        );
        cctx.PrepareCommandBuffer();
        ccb.BindComputeStage(*bloom_compute_stage);
        ccb.DispatchCompute(
            color->GetTextureDescription().width / 16 + 1, color->GetTextureDescription().height / 16 + 1, 1
        );

        context.UseImage(
            *color,
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::ShaderRandomWrite
        );
        context.PrepareCommandBuffer();
        gsys->DrawGUI(
            {color.get(), nullptr, AttachmentUtils::LoadOperation::Load, AttachmentUtils::StoreOperation::Store},
            extent,
            cb
        );
        cb.End();

        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageBlitComposition(
            color->GetImage(),
            vk::Extent2D{color->GetTextureDescription().width, color->GetTextureDescription().height},
            rsys->GetSwapchain().GetExtent()
        );
        rsys->CompleteFrame();

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
