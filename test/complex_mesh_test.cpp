#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <tiny_obj_loader.h>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Material/TestMaterialWithTransform.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/PremadePipeline/SingleRenderPassWithDepth.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

Engine::MainClass * cmc;

class MeshComponentFromFile : public MeshComponent {
    Transform transform;

    void LoadMesh(std::filesystem::path mesh) {
        tinyobj::ObjReaderConfig reader_config{};
        tinyobj::ObjReader reader{};

        m_materials.clear();
        m_submeshes.clear();

        std::shared_ptr<Material> mat = std::make_shared<TestMaterialWithTransform>(m_system);
        std::shared_ptr<HomogeneousMesh> hmsh = std::make_shared<HomogeneousMesh>(m_system);
        m_materials.push_back(mat);
        m_submeshes.push_back(hmsh);

        std::vector <VertexStruct::VertexPosition> positions;
        std::vector <VertexStruct::VertexAttribute> attributes;
        std::vector <uint32_t> indices;

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
        // const auto & materials = reader.GetMaterials();

        assert(shapes.size() == 1);
        for (size_t shp = 0; shp < shapes.size(); shp++) {
            const auto & shape = shapes[shp];
            auto shape_vertices_size = shape.mesh.num_face_vertices.size();

            positions.resize(shape_vertices_size * 3);
            attributes.resize(shape_vertices_size * 3);
            indices.resize(shape_vertices_size * 3);

            for (size_t fc = 0; fc < shape_vertices_size; fc++) {
                unsigned int face_vertex_count = shape.mesh.num_face_vertices[fc];
                assert(face_vertex_count == 3);
                for (unsigned int vrtx = 0; vrtx < face_vertex_count; vrtx++) {
                    tinyobj::index_t index = shape.mesh.indices[fc * 3 + vrtx];
                    // assert(index.vertex_index == index.normal_index && index.vertex_index == index.texcoord_index);
                    float x{attrib.vertices[size_t(index.vertex_index)*3+0]};
                    float y{attrib.vertices[size_t(index.vertex_index)*3+1]};
                    float z{attrib.vertices[size_t(index.vertex_index)*3+2]};
                    
                    assert(index.texcoord_index >= 0);
                    float uv_u{attrib.texcoords[size_t(index.texcoord_index)*2+0]};
                    float uv_v{attrib.texcoords[size_t(index.texcoord_index)*2+1]};

                    positions[fc * 3 + vrtx] = VertexStruct::VertexPosition{.position = {x, y, z}};
                    attributes[fc * 3 + vrtx] = VertexStruct::VertexAttribute{.color = {1.0f, 0.0f, 0.0f}, .texcoord1 = {uv_u, uv_v}};
                    indices[fc * 3 + vrtx] = fc * 3 + vrtx;
                }
            }
        }
        hmsh->SetPositions(positions);
        hmsh->SetAttributes(attributes);
        hmsh->SetIndices(indices);
    }

public: 
    MeshComponentFromFile(std::weak_ptr <RenderSystem> system, std::filesystem::path mesh_file_name) 
    : MeshComponent(std::weak_ptr<GameObject>(), system), transform() {
        transform.SetPosition(glm::vec3{0.0, 0.0, 0.0});
        transform.SetScale(glm::vec3{2.0f, 2.0f, 2.0f});
        LoadMesh(mesh_file_name);

        auto & tcb = system.lock()->GetTransferCommandBuffer();
        tcb.Begin();
        for (auto & submesh : m_submeshes) {
            submesh->Prepare();
            tcb.CommitVertexBuffer(*submesh);
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
    std::filesystem::path mesh_path{ENGINE_ASSETS_DIR};
    mesh_path /= "bunny/bunny.obj";
    std::shared_ptr tmc = std::make_shared<MeshComponentFromFile>(system, mesh_path);
    system->RegisterComponent(tmc);

    // Setup camera
    auto camera_go = std::make_shared<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, -1.0f, 0.0f});
    camera_go->SetTransform(transform);
    auto camera_comp = std::make_shared<CameraComponent>(camera_go);
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
        cb.BeginRenderPass(rts, extent, index);
        system->DrawMeshes(in_flight_frame_id);
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
