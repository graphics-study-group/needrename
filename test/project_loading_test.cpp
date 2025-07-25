#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>

#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Scene/GameObjectAsset.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Framework/world/WorldSystem.h>
#include <Functional/SDLWindow.h>
#include <MainClass.h>
#include <Render/RenderSystem.h>
#include <cmake_config.h>

using namespace Engine;

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Project Loading Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->GetAssetManager()->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    cmc->GetAssetManager()->LoadBuiltinAssets();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");
    cmc->LoopFinite(max_frame_count);

    return 0;
}
