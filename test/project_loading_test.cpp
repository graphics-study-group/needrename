#include <SDL3/SDL.h>
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
#include <Framework/component/RenderComponent/CameraComponent.h>

using namespace Engine;

int main()
{
    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "External Resource Loading Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);
    
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");
    cmc->LoopFiniteFrame(10000);

    return 0;
}
