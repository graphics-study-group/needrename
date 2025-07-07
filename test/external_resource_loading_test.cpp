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
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Render/Renderer/Camera.h>

using namespace Engine;

int main()
{
    std::filesystem::path project_path(ENGINE_TESTS_DIR);
    project_path = project_path / "external_resource_loading_test_project";
    if (std::filesystem::exists(project_path))
        std::filesystem::remove_all(project_path);
    std::filesystem::copy(std::filesystem::path(ENGINE_PROJECTS_DIR) / "empty_project", project_path, std::filesystem::copy_options::recursive);

    std::filesystem::path mesh_path(ENGINE_ASSETS_DIR);
    mesh_path = mesh_path / "four_bunny" / "four_bunny.obj";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "External Resource Loading Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->GetAssetManager()->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    cmc->GetAssetManager()->LoadBuiltinAssets();

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
    cmc->LoopFinite(10000, 0.0f);

    std::filesystem::remove_all(project_path);

    return 0;
}
