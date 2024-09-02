#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

#include "cmake_config.h"
#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Asset/Mesh/MeshAsset.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Framework/level/Level.h"
#include "Framework/go/GameObject.h"
#include "Framework/component/RenderComponent/MeshComponent.h"

#include "Render/RenderSystem.h"
#include "Render/Material/TestMaterial.h"
#include "Render/Pipeline/PremadePipeline/DefaultPipeline.h"
#include "Render/Pipeline/PremadePipeline/SingleRenderPass.h"
#include "Render/Renderer/HomogeneousMesh.h"

using namespace Engine;

Engine::MainClass * cmc;

int main(int argc, char * argv[])
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_LogInfo(0, "Loading mesh...");

    StartupOptions opt;
    opt.resol_x = 800;
    opt.resol_y = 600;
    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            SDL_LOG_PRIORITY_VERBOSE
    );
    cmc->Initialize(&opt);

    auto asset_manager = cmc->GetAssetManager();
    auto render_system = cmc->GetRenderSystem();
    std::filesystem::path project_path(ENGINE_ROOT_DIR);
    project_path = project_path / "test_project";
    cmc->GetAssetManager()->LoadProject(project_path);

    // Load an mesh GO
    nlohmann::json prefab_json;
    std::ifstream prefab_file(project_path / "assets" / "four_bunny.prefab.asset");
    prefab_file >> prefab_json;
    prefab_file.close();
    nlohmann::json component_json = prefab_json["components"][0];
    std::shared_ptr<GameObject> test_mesh_go = std::make_shared<GameObject>();
    std::shared_ptr<MeshComponent> mesh_component = std::make_shared<MeshComponent>(test_mesh_go, render_system);
    test_mesh_go->AddComponent(mesh_component);
    MeshAsset mesh {asset_manager};
    mesh.SetGUID(stringToGUID(component_json["mesh"]));

    mesh.Load();
    mesh_component->Materialize(mesh);
    mesh.Unload();
    

    // Load level
    std::shared_ptr<Level> level = std::make_shared<Level>(asset_manager);
    level->AddGameObject(test_mesh_go);
    nlohmann::json level_json;
    std::ifstream level_file(project_path / "assets" / "default_level.level.asset");
    level_file >> level_json;
    level_file.close();
    level->SetGUID(stringToGUID(level_json["guid"]));
    
    level->Load();
    level->Unload();

    // Try render the mesh
    PipelineLayout pl{render_system};
    pl.CreatePipelineLayout({}, {});

    SingleRenderPass rp{render_system};
    rp.CreateFramebuffersFromSwapchain();
    rp.SetClearValues({{{0.0f, 0.0f, 0.0f, 1.0f}}});

    TestMaterial material{render_system, rp};

    uint32_t in_flight_frame_id = 0;
    uint32_t total_test_frame = 360;
    
    render_system->UpdateSwapchain();
    rp.CreateFramebuffersFromSwapchain();

    mesh_component->GetSubmesh(0)->Prepare();
    
    while(true) {

        render_system->WaitForFrameBegin(in_flight_frame_id);
        RenderCommandBuffer & cb = render_system->GetGraphicsCommandBuffer(in_flight_frame_id);

        uint32_t index = render_system->GetNextImage(in_flight_frame_id, 0x7FFFFFFF);
        assert(index < 3);
    
        cb.Begin();
        if (mesh_component->GetSubmesh(0)->NeedCommitment()) {
            cb.CommitVertexBuffer(*(mesh_component->GetSubmesh(0)));
        }
        vk::Extent2D extent {render_system->GetSwapchain().GetExtent()};
        cb.BeginRenderPass(rp, extent, index);

        cb.BindMaterial(material, 0);
        vk::Rect2D scissor{{0, 0}, render_system->GetSwapchain().GetExtent()};
        cb.SetupViewport(extent.width, extent.height, scissor);
        cb.DrawMesh(*(mesh_component->GetSubmesh(0)));
        cb.End();

        cb.SubmitToQueue(render_system->getQueueInfo().graphicsQueue, render_system->getSynchronization());

        render_system->Present(index, in_flight_frame_id);

        in_flight_frame_id = (in_flight_frame_id + 1) % 3;
    }
    render_system->WaitForIdle();

    delete cmc;
    return 0;
}

