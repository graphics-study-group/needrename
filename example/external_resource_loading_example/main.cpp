#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Loader/Importer.h>
#include <Asset/Scene/GameObjectAsset.h>
#include <Core/Functional/SDLWindow.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/RenderSystem.h>
#include <Render/Renderer/Camera.h>
#include <cmake_config.h>

using namespace Engine;

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    std::filesystem::path project_path(ENGINE_EXAMPLES_DIR);
    project_path = project_path / "external_resource_loading_example" / "temp_project";
    if (std::filesystem::exists(project_path)) std::filesystem::remove_all(project_path);
    std::filesystem::copy(
        std::filesystem::path(ENGINE_PROJECTS_DIR) / "empty_project",
        project_path,
        std::filesystem::copy_options::recursive
    );

    std::filesystem::path mesh_path(ENGINE_ASSETS_DIR);
    mesh_path = mesh_path / "four_bunny" / "four_bunny.obj";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "External Resource Loading Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    auto asys = cmc->GetAssetManager();
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Importing external resource");
    std::filesystem::path path_in_project = "/";
    Engine::Importer::ImportExternalResource(mesh_path, path_in_project);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading the prefab which has just imported");
    std::filesystem::path prefab_path = path_in_project / ("GO_" + mesh_path.stem().string() + ".asset");
    auto prefab_ref = adb->GetNewAssetRef(prefab_path);
    asys->LoadAssetImmediately(prefab_ref);
    cmc->GetWorldSystem()->LoadGameObjectAsset(prefab_ref->as<GameObjectAsset>());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Setting up camera");
    auto camera_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, 1.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    camera_go->SetTransform(transform);
    auto camera_comp = camera_go->template AddComponent<CameraComponent>();
    camera_comp->m_camera->set_aspect_ratio(1.0 * opt.resol_x / opt.resol_y);
    cmc->GetWorldSystem()->m_active_camera = camera_comp->m_camera;
    cmc->GetWorldSystem()->AddGameObjectToWorld(camera_go);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");
    cmc->LoopFinite(max_frame_count, 0.0f);

    std::filesystem::remove_all(project_path);

    return 0;
}
