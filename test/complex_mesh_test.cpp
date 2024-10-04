#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <tiny_obj_loader.h>
#include <stb_image.h>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Memory/Image2DTexture.h"
#include "Render/Material/Shadeless.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

Engine::MainClass * cmc;

class MeshComponentFromFile : public MeshComponent {
    Transform transform;

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
            auto ptr = std::make_shared<Shadeless>(m_system);
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

                    positions[material_id].push_back(VertexStruct::VertexPosition{.position = {x, y, z}});
                    attributes[material_id].push_back(VertexStruct::VertexAttribute{.color = {1.0f, 1.0f, 1.0f}, .texcoord1 = {uv_u, uv_v}});
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
    MeshComponentFromFile(std::weak_ptr <RenderSystem> system, std::filesystem::path mesh_file_name) 
    : MeshComponent(std::weak_ptr<GameObject>(), system), transform() {
        transform.SetPosition(glm::vec3{0.0, 0.0, -0.5});
        transform.SetScale(glm::vec3{.5f, .5f, .5f});
        LoadMesh(mesh_file_name);

        auto & tcb = system.lock()->GetTransferCommandBuffer();
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
            m_textures[mat]->Create(tex_width, tex_height, vk::Format::eR8G8B8A8Srgb);
            tcb.CommitTextureImage(*m_textures[mat], reinterpret_cast<std::byte *>(raw_image_data), tex_width * tex_height * 4);

            // Write descriptors
            auto mat_ptr = std::dynamic_pointer_cast<Shadeless>(m_materials[mat]);
            assert(mat_ptr);
            mat_ptr->UpdateTexture(*m_textures[mat]);

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
};

int main(int, char **)
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE);
    cmc->Initialize(&opt);

    auto system = cmc->GetRenderSystem();
    system->EnableDepthTesting();

    RenderTargetSetup rts{system};
    rts.CreateFromSwapchain();
    rts.SetClearValues({
        vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}},
        vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0U}}
    });
    
    // Setup mesh
    std::filesystem::path mesh_path{"C:\\Users\\Vincent Lee\\3D Objects\\furina\\obj\\furina_combined.obj"};
    std::shared_ptr tmc = std::make_shared<MeshComponentFromFile>(system, mesh_path);
    system->RegisterComponent(tmc);

    // Setup camera
    auto camera_go = std::make_shared<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, -1.0f, 0.0f});
    camera_go->SetTransform(transform);
    auto camera_comp = std::make_shared<CameraComponent>(camera_go);
    camera_comp->set_aspect_ratio(1920.0 / 1080.0);
    camera_go->AddComponent(camera_comp);
    system->SetActiveCamera(camera_comp);

    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 60;
    uint64_t start_timer = SDL_GetPerformanceCounter();
    while(total_test_frame--) {
        
        auto frame_start_timer = sch::high_resolution_clock::now();

        system->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = system->GetGraphicsCommandBuffer(in_flight_frame_id);
        uint32_t index = system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);

        assert(index < 3);
    
        cb.Begin();
        vk::Extent2D extent {system->GetSwapchain().GetExtent()};
        cb.BeginRendering(rts, extent, index);
        system->DrawMeshes(in_flight_frame_id);
        cb.EndRendering();
        cb.End();
        cb.Submit();

        system->Present(index, in_flight_frame_id);

        auto submission_end_timer = sch::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> total;
        total = submission_end_timer - frame_start_timer;
        SDL_LogVerbose(0, "Total: %lf milliseconds, or %lf fps for frame %u.", 
            total.count(),
            1000.0 / total.count(),
            in_flight_frame_id
            );

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
    }
    uint64_t end_timer = SDL_GetPerformanceCounter();
    uint64_t duration = end_timer - start_timer;
    double duration_time = 1.0 * duration / SDL_GetPerformanceFrequency();
    SDL_LogInfo(0, "Took %lf seconds for 200 frames (avg. %lf fps).", duration_time, 200.0 / duration_time);
    system->WaitForIdle();
    system->ClearComponent();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;
    return 0;
}
