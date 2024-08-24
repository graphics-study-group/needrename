#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

#include "cmake_config.h"
#include "MainClass.h"
#include "GlobalSystem.h"
#include "Functional/SDLWindow.h"

#include "Asset/AssetManager/AssetManager.h"
#include "Framework/level/Level.h"
#include "Framework/component/RenderComponent/MeshComponent.h"
#include "Render/Material/Shadeless.h"
#include "Render/RenderSystem.h"

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

    std::filesystem::path project_path(ENGINE_ROOT_DIR);
    project_path = project_path / "test_project";
    globalSystems.assetManager->LoadProject(project_path);

    // Load an mesh GO
    nlohmann::json prefab_json;
    std::ifstream prefab_file(project_path / "assets" / "four_bunny.prefab.asset");
    prefab_file >> prefab_json;
    prefab_file.close();
    nlohmann::json component_json = prefab_json["components"][0];
    std::shared_ptr<GameObject> test_mesh_go = std::make_shared<GameObject>();
    std::shared_ptr<MeshComponent> mesh_component = std::make_shared<MeshComponent>(test_mesh_go);
    test_mesh_go->AddComponent(mesh_component);
    MeshAsset mesh;
    mesh.SetGUID(stringToGUID(component_json["mesh"]));
    mesh_component->SetMesh(std::make_shared<MeshAsset>(mesh));
    for(auto & material_guid : component_json["materials"])
    {
        std::shared_ptr<ShadelessMaterial> mat = std::make_shared<ShadelessMaterial>();
        mat->SetGUID(stringToGUID(material_guid));
        mesh_component->AddMaterial(mat);
    }

    globalSystems.renderer->RegisterComponent(mesh_component);

    // Load level
    std::shared_ptr<Level> level = std::make_shared<Level>();
    level->AddGameObject(test_mesh_go);
    nlohmann::json level_json;
    std::ifstream level_file(project_path / "assets" / "default_level.level.asset");
    level_file >> level_json;
    level_file.close();
    level->SetGUID(stringToGUID(level_json["guid"]));
    
    level->Load();

    level->Unload();

    delete cmc;
    return 0;
}

