#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <tiny_obj_loader.h>

#include "MainClass.h"
#include "Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Material/TestMaterial.h"
#include "Render/Pipeline/Shader.h"
#include "Render/Pipeline/RenderTarget/RenderTargetSetup.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/PremadePipeline/SingleRenderPassWithDepth.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

Engine::MainClass * cmc;

class TestHomoMesh : public HomogeneousMesh {
public:
    TestHomoMesh(std::weak_ptr<RenderSystem> system) : HomogeneousMesh(system) {
        this->m_positions = {{0.0f, -0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}};
        this->m_attributes = {
            {.color = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}, 
            {.color = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}, 
            {.color = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 0.0f, 0.0f}, .texcoord1 = {0.0f, 0.0f}}
        };
        this->m_indices = {0, 1, 2};
    }
};

class MeshComponentFromFile : public MeshComponent {
    Transform transform;

    void LoadMesh(std::filesystem::path mesh) {
        tinyobj::ObjReaderConfig reader_config{};
        tinyobj::ObjReader reader{};

        m_materials.clear();
        m_submeshes.clear();

        // std::shared_ptr<Material> mat = std::make_shared<TestMaterial>(m_system);
        // std::shared_ptr<HomogeneousMesh> hmsh = std::make_shared<HomogeneousMesh>(m_system);
        // m_materials.push_back(mat);
        // m_submeshes.push_back(hmsh);

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

        assert(shapes.size() == 1);
        for (size_t shp = 0; shp < shapes.size(); shp++) {
            const auto & shape = shapes[shp];
            for (size_t fc = 0; fc < shape.mesh.num_face_vertices.size(); fc++) {
                unsigned int face_vertex_count = shape.mesh.num_face_vertices[fc];
                assert(face_vertex_count == 3);
                for (unsigned int vrtx = 0; vrtx < face_vertex_count; vrtx++) {
                    tinyobj::index_t index = shape.mesh.indices[vrtx];
                    assert(index.vertex_index == index.normal_index && index.vertex_index == index.texcoord_index);
                }
            }
        }

    }

public: 
    MeshComponentFromFile(std::weak_ptr <RenderSystem> system, std::filesystem::path mesh_file_name) 
    : MeshComponent(std::weak_ptr<GameObject>(), system), transform() {
        transform.SetPosition(glm::vec3{1.0, 0.0, 0.0});
        LoadMesh(mesh_file_name);

        for (auto & submesh : m_submeshes) {
            submesh->Prepare();
        }
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

    std::filesystem::path mesh_path{ENGINE_ASSETS_DIR};
    mesh_path /= "bunny/bunny.obj";
    std::shared_ptr tmc = std::make_shared<MeshComponentFromFile>(system, mesh_path);
    system->RegisterComponent(tmc);

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
