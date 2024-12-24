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
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/Material/BlinnPhong.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "GUI/GUISystem.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

class MeshComponentFromFile : public MeshComponent {
    Transform transform;
    BlinnPhong::UniformData m_uniform_data {
        glm::vec4{0.5, 0.5, 0.5, 4.0}, 
        glm::vec4{0.1, 0.1, 0.1, 1.0}
    };
    std::vector <std::unique_ptr<AllocatedImage2DTexture>> m_textures {};
    std::vector <std::filesystem::path> m_texture_files {};

    void LoadMesh(std::filesystem::path mesh) {
        tinyobj::ObjReaderConfig reader_config{};
        tinyobj::ObjReader reader{};

        m_materials.clear();
        m_submeshes.clear();

        // std::shared_ptr mat = std::make_shared<Shadeless>(m_system);
        // std::shared_ptr hmsh = std::make_shared<HomogeneousMesh>(m_system);
        // m_materials.push_back(mat);
        // m_submeshes.push_back(hmsh);

        std::vector <std::vector<VertexStruct::VertexPosition>> positions;
        std::vector <std::vector<VertexStruct::VertexAttribute>> attributes;
        std::vector <std::vector<uint32_t>> indices;

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

        const auto & attrib = reader.GetAttrib();
        const auto & shapes = reader.GetShapes();
        const auto & materials = reader.GetMaterials();

        // Process materials
        m_textures.clear();
        m_texture_files.clear();
        for (const auto & material : materials) {
            auto ptr = std::make_shared<BlinnPhong>(m_system);
            m_materials.push_back(ptr);

            const std::string & dname = material.diffuse_texname;
            m_texture_files.push_back(mesh.parent_path() / dname);
            auto tex_ptr = std::make_unique<AllocatedImage2DTexture>(m_system);
            m_textures.push_back(std::move(tex_ptr));

            SDL_LogInfo(0, "Material name %s: diffuse map %s.", material.name.c_str(), m_texture_files.rbegin()->string().c_str());
        }
        positions.resize(materials.size());
        attributes.resize(materials.size());
        indices.resize(materials.size());

        // assert(shapes.size() == 1);
        for (size_t shp = 0; shp < shapes.size(); shp++) {
            const auto & shape = shapes[shp];
            auto shape_vertices_size = shape.mesh.num_face_vertices.size();

            for (size_t fc = 0; fc < shape_vertices_size; fc++) {
                unsigned int face_vertex_count = shape.mesh.num_face_vertices[fc];
                assert(face_vertex_count == 3);
                // assert(material_id == shape.mesh.material_ids[fc]);
                auto material_id = shape.mesh.material_ids[fc];
                for (unsigned int vrtx = 0; vrtx < face_vertex_count; vrtx++) {
                    tinyobj::index_t index = shape.mesh.indices[fc * 3 + vrtx];
                    // assert(index.vertex_index == index.normal_index && index.vertex_index == index.texcoord_index);
                    float x{attrib.vertices[size_t(index.vertex_index)*3+0]};
                    float y{attrib.vertices[size_t(index.vertex_index)*3+1]};
                    float z{attrib.vertices[size_t(index.vertex_index)*3+2]};
                    
                    assert(index.texcoord_index >= 0);
                    float uv_u{attrib.texcoords[size_t(index.texcoord_index)*2+0]};
                    float uv_v{attrib.texcoords[size_t(index.texcoord_index)*2+1]};
                    
                    assert(index.normal_index >= 0);
                    float normal_x{attrib.normals[size_t(index.normal_index)*3+0]};
                    float normal_y{attrib.normals[size_t(index.normal_index)*3+1]};
                    float normal_z{attrib.normals[size_t(index.normal_index)*3+2]};

                    positions[material_id].push_back(VertexStruct::VertexPosition{.position = {x, y, z}});
                    attributes[material_id].push_back(VertexStruct::VertexAttribute{
                        .color = {1.0f, 1.0f, 1.0f}, 
                        .normal = {normal_x, normal_y, normal_z}, 
                        .texcoord1 = {uv_u, uv_v}
                    });
                    indices[material_id].push_back(positions[material_id].size() - 1);
                }
            }
        }

        for (size_t mat = 0; mat < materials.size(); mat++) {
            assert(positions[mat].size() % 3 == 0);
            assert(attributes[mat].size() == positions[mat].size() && positions[mat].size() == indices[mat].size());
            m_submeshes.push_back(std::make_shared<HomogeneousMesh>(m_system));
            m_submeshes[mat]->SetPositions(positions[mat]);
            m_submeshes[mat]->SetAttributes(attributes[mat]);
            m_submeshes[mat]->SetIndices(indices[mat]);
        }
    }

public: 
    MeshComponentFromFile(std::filesystem::path mesh_file_name) 
    : MeshComponent(std::weak_ptr<GameObject>()), transform() {
        transform.SetPosition(glm::vec3{0.0, 0.0, -0.5});
        transform.SetScale(glm::vec3{.5f, .5f, .5f});
        LoadMesh(mesh_file_name);

        auto & tcb = m_system.lock()->GetTransferCommandBuffer();
        tcb.Begin();
        for (auto & submesh : m_submeshes) {
            submesh->Prepare();
            tcb.CommitVertexBuffer(*submesh);
        }

        stbi_set_flip_vertically_on_load(true);
        for (size_t mat = 0; mat < m_materials.size(); mat++) {
            // Read images
            SDL_LogInfo(0, "Processing material slot %llu", mat);
            int tex_width, tex_height, tex_channel;
            stbi_uc * raw_image_data = stbi_load(m_texture_files[mat].string().c_str(), &tex_width, &tex_height, &tex_channel, 4);
            assert(raw_image_data);
            m_textures[mat]->Create(tex_width, tex_height, ImageUtils::ImageFormat::R8G8B8A8SRGB);
            tcb.CommitTextureImage(*m_textures[mat], reinterpret_cast<std::byte *>(raw_image_data), tex_width * tex_height * 4);

            // Write descriptors
            auto mat_ptr = std::dynamic_pointer_cast<BlinnPhong>(m_materials[mat]);
            assert(mat_ptr);
            mat_ptr->UpdateTexture(*m_textures[mat]);
            mat_ptr->UpdateUniform(m_uniform_data);

            stbi_image_free(raw_image_data);
        }
        tcb.End();
        tcb.SubmitAndExecute();
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
            auto mat_ptr = std::dynamic_pointer_cast<BlinnPhong>(material);
            assert(mat_ptr);
            mat_ptr->UpdateUniform(m_uniform_data);
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

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto rsys = cmc->GetRenderSystem();
    rsys->EnableDepthTesting();
    auto gsys = cmc->GetGUISystem();

    RenderTargetSetup rts{rsys};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });
    
    // Setup mesh
    std::filesystem::path mesh_path{std::string(ENGINE_ASSETS_DIR) + "/__furina/furina_combined.obj"};
    std::shared_ptr tmc = std::make_shared<MeshComponentFromFile>(mesh_path);
    rsys->RegisterComponent(tmc);

    // Setup camera
    auto camera_go = std::make_shared<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, 1.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    camera_go->SetTransform(transform);
    auto camera_comp = std::make_shared<CameraComponent>(camera_go);
    camera_comp->set_aspect_ratio(1920.0 / 1080.0);
    camera_go->AddComponent(camera_comp);
    rsys->SetActiveCamera(camera_comp);

    uint32_t in_flight_frame_id = 0;
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
        SubmitSceneData(rsys, in_flight_frame_id);
        SubmitMaterialData(tmc);

        // Draw
        rsys->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = rsys->GetGraphicsCommandBuffer(in_flight_frame_id);
        uint32_t index = rsys->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);
        cb.Begin();
        vk::Extent2D extent {rsys->GetSwapchain().GetExtent()};
        cb.BeginRendering(rts, extent, index);
        rsys->DrawMeshes(in_flight_frame_id);
        gsys->DrawGUI(cb);
        cb.EndRendering();
        cb.End();
        cb.Submit();
        rsys->Present(index, in_flight_frame_id);

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
        SDL_Delay(5);

        if (frame_count >= max_frame_count) break;
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
