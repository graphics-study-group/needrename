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
#include "Render/RenderSystem.h"
#include "Asset/AssetManager/AssetManager.h"

using namespace Engine;

Engine::MainClass * cmc;

int main(int argc, char * argv[])
{
    std::filesystem::path project_path(ENGINE_ROOT_DIR);
    project_path = project_path / "test_project";
    std::filesystem::path mesh_path(ENGINE_ASSETS_DIR);
    mesh_path = mesh_path / "four_bunny" / "four_bunny.obj";

    std::filesystem::path loaded_asset_path = project_path / "assets" / "loading_test";
    if (std::filesystem::exists(loaded_asset_path))
        std::filesystem::remove_all(loaded_asset_path);
    if (!std::filesystem::create_directories(loaded_asset_path))
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Failed to create directory %s", loaded_asset_path.c_str());
        return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions * opt = ParseOptions(argc, argv);
    if (opt->instantQuit)
        return -1;

    cmc = new Engine::MainClass(
            SDL_INIT_VIDEO,
            opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);
    cmc->Initialize(opt);

    globalSystems.assetManager->LoadProject(project_path);
    globalSystems.assetManager->LoadExternalResource(mesh_path, std::filesystem::path("loading_test"));
    
    std::filesystem::remove_all(loaded_asset_path);

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
