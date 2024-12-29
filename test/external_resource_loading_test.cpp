#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <fstream>

#include <cmake_config.h>
#include <MainClass.h>
#include <Functional/SDLWindow.h>
#include <Render/RenderSystem.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Scene/GameObjectAsset.h>

using namespace Engine;

int main(int argc, char * argv[])
{
    std::filesystem::path project_path(ENGINE_TESTS_DIR);
    project_path = project_path / "external_resource_loading_test_project";
    if (std::filesystem::exists(project_path))
        std::filesystem::remove_all(project_path);
    std::filesystem::create_directory(project_path);

    std::filesystem::path mesh_path(ENGINE_ASSETS_DIR);
    mesh_path = mesh_path / "four_bunny" / "four_bunny.obj";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions * opt = ParseOptions(argc, argv);
    if (opt->instantQuit)
        return -1;

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(opt, SDL_INIT_VIDEO, opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->GetAssetManager()->LoadProject(project_path);
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
    
    // std::filesystem::remove_all(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;

    return 0;
}
