#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <fstream>

#include <cmake_config.h>
#include <MainClass.h>
#include <Functional/SDLWindow.h>
#include <Render/RenderSystem.h>
#include <Framework/world/WorldSystem.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Scene/GameObjectAsset.h>

using namespace Engine;

void CreateEmptyProject(const std::filesystem::path &project_path)
{
    std::filesystem::create_directory(project_path);
    const char default_level[] = R"(
{
    "%main_id": "&0",
    "%data": {
        "&0": {
            "m_gameobjects": [],
            "Asset::m_guid": "ABCDEF00000000000000000000000000",
            "%type": "Engine::LevelAsset"
        }
    }
}
)";
    const char project_config[] = R"(
{
    "default_level": "ABCDEF00000000000000000000000000"
}
)";
    if (!std::filesystem::exists(project_path / "assets"))
        std::filesystem::create_directory(project_path / "assets");
    std::ofstream project_file(project_path / "project.config");
    project_file << project_config;
    project_file.close();
    std::ofstream level_file(project_path / "assets" / "default_level.level.asset");
    level_file << default_level;
    level_file.close();
}

int main(int argc, char *argv[])
{
    std::filesystem::path project_path(ENGINE_TESTS_DIR);
    project_path = project_path / "external_resource_loading_test_project";
    if (std::filesystem::exists(project_path))
        std::filesystem::remove_all(project_path);
    CreateEmptyProject(project_path);

    std::filesystem::path mesh_path(ENGINE_ASSETS_DIR);
    mesh_path = mesh_path / "four_bunny" / "four_bunny.obj";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions *opt = ParseOptions(argc, argv);
    if (opt->instantQuit)
        return -1;

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(opt, SDL_INIT_VIDEO, opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Importing external resource");
    cmc->GetAssetManager()->ImportExternalResource(mesh_path, std::filesystem::path("."));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading the prefab which has just imported");
    std::filesystem::path prefab_path = cmc->GetAssetManager()->GetAssetsDirectory() / mesh_path.filename();
    prefab_path.replace_extension(".gameobject.asset");
    nlohmann::json prefab_json;
    std::ifstream prefab_file(prefab_path);
    prefab_file >> prefab_json;
    prefab_file.close();
    GUID prefab_guid(prefab_json["%data"]["&0"]["Asset::m_guid"].get<std::string>());
    auto prefab_asset = dynamic_pointer_cast<GameObjectAsset>(cmc->GetAssetManager()->LoadAssetImmediately(prefab_guid));

    cmc->GetWorldSystem()->LoadGameObjectAsset(prefab_asset);
    
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");
    cmc->MainLoop();

    // std::filesystem::remove_all(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;

    return 0;
}
