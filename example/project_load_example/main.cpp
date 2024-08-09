#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <fstream>

#include "cmake_config.h"
#include "consts.h"
#include "MainClass.h"
#include "GlobalSystem.h"
#include "Functional/SDLWindow.h"
#include "Framework/go/GameObject.h"
#include "Framework/level/Level.h"
#include "Framework/world/WorldSystem.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/RenderSystem.h"
#include "Render/Material/Shadeless.h"
#include "Asset/AssetManager/AssetManager.h"

using namespace Engine;

class TestMesh : public GameObject
{

};

Engine::MainClass * cmc;

int main(int argc, char * argv[])
{
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions * opt = ParseOptions(argc, argv);
    if (opt->instantQuit)
        return -1;

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);
    SDLWindow::EnableMSAA(4);
    cmc->Initialize(opt);

    std::filesystem::path project_path(ENGINE_ROOT_DIR);
    project_path = project_path / "test_project";
    globalSystems.assetManager->LoadProject(project_path);

    /// XXX: should use reflection to load prefab. This is just a test.
    nlohmann::json prefab_json;
    std::ifstream prefab_file(project_path / "assets" / "four_bunny.prefab.asset");
    prefab_file >> prefab_json;
    prefab_file.close();
    nlohmann::json component_json = prefab_json["components"][0];
    std::shared_ptr<TestMesh> test_mesh_go = std::make_shared<TestMesh>();
    std::shared_ptr<MeshComponent> mesh_component = std::make_shared<MeshComponent>(test_mesh_go);
    test_mesh_go->AddComponent(mesh_component);
    Mesh mesh;
    mesh.SetGUID(stringToGUID(component_json["mesh"]));
    mesh_component->SetMesh(std::make_shared<Mesh>(mesh));
    for(auto & material_guid : component_json["materials"])
    {
        std::shared_ptr<ShadelessMaterial> mat = std::make_shared<ShadelessMaterial>();
        mat->SetGUID(stringToGUID(material_guid));
        mesh_component->AddMaterial(mat);
    }

    // Add the game object to the render system and world system
    globalSystems.renderer->RegisterComponent(mesh_component);
    std::shared_ptr<Level> level = std::make_shared<Level>();
    level->AddGameObject(test_mesh_go);
    globalSystems.world->SetCurrentLevel(level);
    
    level->Load();
    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
