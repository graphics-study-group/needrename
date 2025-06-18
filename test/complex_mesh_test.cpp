#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <tiny_obj_loader.h>
#include <stb_image.h>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/FullRenderSystem.h"
#include "GUI/GUISystem.h"
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include "Asset/Material/MaterialAsset.h"
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Loader/ObjLoader.h>

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

class MeshComponentFromFile : public MeshComponent {
    Transform transform;

    struct UniformData {
        glm::vec4 specular;
        glm::vec4 ambient;
    };

    UniformData m_uniform_data {
        glm::vec4{0.5, 0.5, 0.5, 4.0}, 
        glm::vec4{0.1, 0.1, 0.1, 1.0}
    };

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
            for (const auto & [_, new_shape] : material_id_map) {
                shapes.push_back(new_shape);
                materials.push_back(origin_materials[new_shape.mesh.material_ids[0]]);
                std::fill(
                    shapes.back().mesh.material_ids.begin(),
                    shapes.back().mesh.material_ids.end(),
                    materials.size() - 1
                );
            }
        }

        this->m_mesh_asset = std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(std::make_shared<MeshAsset>()));
        ObjLoader loader;
        loader.LoadMeshAssetFromTinyObj(*(this->m_mesh_asset->as<MeshAsset>()), attrib, shapes);

        // Read material assets
        for (const auto & material : materials) {
            this->m_material_assets.push_back(std::make_shared<AssetRef>(std::dynamic_pointer_cast<Asset>(std::make_shared<MaterialAsset>())));
            loader.LoadMaterialAssetFromTinyObj(*(this->m_material_assets.back()->as<MaterialAsset>()), material, mesh.parent_path());
        }

        assert(m_mesh_asset && m_mesh_asset->IsValid());
        m_submeshes.clear();
        size_t submesh_count = m_mesh_asset->as<MeshAsset>()->GetSubmeshCount();
        for (size_t i = 0; i < submesh_count; i++)
        {
            m_submeshes.push_back(std::make_shared<HomogeneousMesh>(
                m_system, m_mesh_asset, i));
        }
    }

public: 
    MeshComponentFromFile(std::filesystem::path mesh_file_name) 
    : MeshComponent(std::weak_ptr<GameObject>()), transform() {
        LoadMesh(mesh_file_name);

        auto system = m_system.lock();
        auto & helper = system->GetFrameManager().GetSubmissionHelper();

        for (auto & submesh : m_submeshes) {
            submesh->Prepare();
            helper.EnqueueVertexBufferSubmission(*submesh);
        }

        for (size_t i = 0; i < m_material_assets.size(); i++) {
            auto ptr = std::make_shared<Materials::BlinnPhongInstance>(m_system, system->GetMaterialRegistry().GetMaterial("Built-in Blinn-Phong"));
            auto mat_asset = m_material_assets[i]->cas<MaterialAsset>();
            assert(mat_asset);
            ptr->Instantiate(*mat_asset);
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

    void UpdateUniformData(float spec_r, float spec_g, float spec_b, float spec_coef) {
        uint8_t identity = 
            (fabs(spec_r - m_uniform_data.specular.r) < 1e-3) +
            (fabs(spec_g - m_uniform_data.specular.g) < 1e-3) +
            (fabs(spec_b - m_uniform_data.specular.b) < 1e-3) +
            (fabs(spec_coef - m_uniform_data.specular.a) < 1e-3);
        if (identity == 4)  return;

        m_uniform_data.specular = glm::vec4{spec_r, spec_g, spec_b, spec_coef};
        for (auto & material : m_materials) {
            auto mat_ptr = std::dynamic_pointer_cast<Materials::BlinnPhongInstance>(material);
            assert(mat_ptr);
            mat_ptr->SetAmbient(m_uniform_data.ambient);
            mat_ptr->SetSpecular(m_uniform_data.specular);
        }
    }
};

struct {
    float zenith, azimuth;
    float r,g,b,coef;
} g_SceneData {M_PI_2, M_PI_2, 0.5f, 0.5f, 0.5f, 4.0f};

glm::vec3 GetCartesian(float zenith, float azimuth) {
    static constexpr float RADIUS = 2.0f;
    return glm::vec3{
        RADIUS * sin(zenith) * cos(azimuth),
        RADIUS * sin(zenith) * sin(azimuth),
        RADIUS * cos(zenith)
    };
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

    ImGui::ColorPicker3("Specular color", &g_SceneData.r);
    ImGui::SliderFloat("Specular strength", &g_SceneData.coef, 0.0f, 64.0f);
    ImGui::End();
}

void SubmitSceneData(std::shared_ptr <RenderSystem> rsys, uint32_t id) {
    ConstantData::PerSceneStruct scene {
        1,
        glm::vec4{
            GetCartesian(g_SceneData.zenith, g_SceneData.azimuth),
            0.0f
        },
        glm::vec4{1.0, 1.0, 1.0, 0.0},
    };
    auto ptr = rsys->GetGlobalConstantDescriptorPool().GetPerSceneConstantMemory(id);
    memcpy(ptr, &scene, sizeof scene);
    rsys->GetGlobalConstantDescriptorPool().FlushPerSceneConstantMemory(id);    
}

void SubmitMaterialData(std::shared_ptr <MeshComponentFromFile> mesh) {
    mesh->UpdateUniformData(g_SceneData.r, g_SceneData.g, g_SceneData.b, g_SceneData.coef);
}

int main(int argc, char ** argv)
{
    SDL_Init(SDL_INIT_VIDEO);

    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto asys = cmc->GetAssetManager();
    asys->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    asys->LoadBuiltinAssets();
    
    auto test_asset = asys->GetNewAssetRef(std::filesystem::path("~/material_templates/BlinnPhongTemplate.asset"));
    asys->LoadAssetImmediately(test_asset);
    asys->LoadAssetsInQueue();

    auto rsys = cmc->GetRenderSystem();
    rsys->GetMaterialRegistry().AddMaterial(test_asset);

    auto gsys = cmc->GetGUISystem();

    Engine::Texture color{*rsys}, depth{*rsys};
    Engine::Texture::TextureDesc desc {
        .dimensions = 2,
        .width = 1280,
        .height = 720,
        .depth = 1,
        .format = Engine::ImageUtils::ImageFormat::B8G8R8A8SRGB,
        .type = Engine::ImageUtils::ImageType::ColorAttachment,
        .mipmap_levels = 1,
        .array_layers = 1,
        .is_cube_map = false
    };
    color.CreateTexture(desc, "Color Attachment");
    desc.format = Engine::ImageUtils::ImageFormat::D32SFLOAT;
    desc.type = Engine::ImageUtils::ImageType::DepthImage;
    depth.CreateTexture(desc, "Depth Attachment");

    Engine::AttachmentUtils::AttachmentDescription color_att, depth_att;
    color_att.image = color.GetImage();
    color_att.image_view = color.GetImageView();
    color_att.load_op = vk::AttachmentLoadOp::eClear;
    color_att.store_op = vk::AttachmentStoreOp::eStore;

    depth_att.image = depth.GetImage();
    depth_att.image_view = depth.GetImageView();
    depth_att.load_op = vk::AttachmentLoadOp::eClear;
    depth_att.store_op = vk::AttachmentStoreOp::eDontCare;


    // Setup mesh
    std::filesystem::path mesh_path{std::string(ENGINE_ASSETS_DIR) + "/four_bunny/four_bunny.obj"};
    std::shared_ptr tmc = std::make_shared<MeshComponentFromFile>(mesh_path);
    rsys->RegisterComponent(tmc);

    // Setup camera
    auto camera_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, 1.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    camera_go->SetTransform(transform);
    auto camera_comp = std::make_shared<CameraComponent>(camera_go);
    camera_comp->set_aspect_ratio(1920.0 / 1080.0);
    camera_go->AddComponent(camera_comp);
    rsys->SetActiveCamera(camera_comp);

    uint64_t frame_count = 0;
    uint64_t start_timer = SDL_GetPerformanceCounter();
    while(++frame_count) {
        bool quited = false;
        SDL_Event event;
        while(SDL_PollEvent(&event) != 0) {
            switch(event.type) {
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
        GraphicsCommandBuffer & cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());

        cb.Begin();
        context.UseImage(color, GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite, GraphicsContext::ImageAccessType::None);
        context.UseImage(depth, GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite, GraphicsContext::ImageAccessType::None);
        context.PrepareCommandBuffer();
        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        cb.BeginRendering({
                color.GetImage(),
                color.GetImageView(),
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore
            }, depth_att, extent);
        rsys->DrawMeshes();
        cb.EndRendering();

        context.UseImage(color, GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite, GraphicsContext::ImageAccessType::ColorAttachmentWrite);
        context.PrepareCommandBuffer();
        gsys->DrawGUI({
                color.GetImage(),
                color.GetImageView(),
                vk::AttachmentLoadOp::eLoad,
                vk::AttachmentStoreOp::eStore
            }, extent, cb);

        cb.End();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->GetFrameManager().StageCopyComposition(color.GetImage());
        // rsys->GetFrameManager().CopyToFrameBuffer(color.GetImage(), rsys->GetSwapchain().GetExtent(), {0, 0}, {100, 100});
        rsys->GetFrameManager().CompositeToFramebufferAndPresent();

        SDL_Delay(5);

        if ((int64_t)frame_count >= max_frame_count) break;
    }
    uint64_t end_timer = SDL_GetPerformanceCounter();
    uint64_t duration = end_timer - start_timer;
    double duration_time = 1.0 * duration / SDL_GetPerformanceFrequency();
    SDL_LogInfo(0, "Took %lf seconds for %llu frames (avg. %lf fps).", duration_time, frame_count, frame_count * 1.0 / duration_time);
    rsys->WaitForIdle();
    rsys->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    return 0;
}
