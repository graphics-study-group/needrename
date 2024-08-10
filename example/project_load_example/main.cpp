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
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Render/RenderSystem.h"
#include "Render/Material/Shadeless.h"
#include "Asset/AssetManager/AssetManager.h"

using namespace Engine;

class Camera : public GameObject
{
public:
    void Tick(float dt) override
    {
        if(orbit_angle > 2 * pi)
            orbit_angle -= 2 * pi;
        orbit_angle += dt / 2.0;
        auto & transform = m_transformComponent->GetTransformRef();
        glm::vec3 pos, rot;
        pos = glm::vec3(radius * glm::sin(orbit_angle), radius * glm::cos(orbit_angle), 0.05f);
        rot = glm::vec3(0.0f, 0.0f, pi - orbit_angle);
        transform.SetPosition(pos).SetRotationEuler(rot);

        SDL_LogInfo(0, "Rotation angle: %f, delta-t: %f", glm::degrees(orbit_angle), dt);
    }

    void Initialize()
    {
        auto & transform = m_transformComponent->GetTransformRef();
        glm::vec3 pos, rot;
        pos = glm::vec3(radius * glm::sin(orbit_angle), radius * glm::cos(orbit_angle), 0.05f);
        rot = glm::vec3(0.0f, 0.0f, pi - orbit_angle);
        transform.SetPosition(pos).SetRotationEuler(rot);

        std::shared_ptr <CameraComponent> cc = std::make_shared<CameraComponent>(weak_from_this());
        AddComponent(std::dynamic_pointer_cast<Component>(cc));
        globalSystems.renderer->SetActiveCamera(cc);
    }

protected:
    float pi {glm::pi<float>()};
    float orbit_angle {0.0f};
    float radius{1.0f};

};

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
    glEnable(GL_DEPTH_TEST);

    std::filesystem::path project_path(ENGINE_ROOT_DIR);
    project_path = project_path / "test_project";
    globalSystems.assetManager->LoadProject(project_path);

    // globalSystems.assetManager->LoadExternalResource(std::filesystem::path(ENGINE_ROOT_DIR) / "assets" / "bunny" / "bunny.obj", "bunny");
    // globalSystems.assetManager->LoadExternalResource(std::filesystem::path(ENGINE_ROOT_DIR) / "assets" / "__noupload" / "keqing" / "mesh.obj", "keqing");

    // Load an mesh GO
    // XXX: should use reflection to load prefab. This is just a test.
    nlohmann::json prefab_json;

    std::ifstream prefab_file(project_path / "assets" / "four_bunny.prefab.asset");
    // std::ifstream prefab_file(project_path / "assets" / "bunny" / "bunny.prefab.asset");
    // std::ifstream prefab_file(project_path / "assets" / "keqing" / "mesh.prefab.asset");

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

    globalSystems.renderer->RegisterComponent(mesh_component);

    // Load camera
    std::shared_ptr<Camera> camera = std::make_shared<Camera>();
    camera->Initialize();

    // Load level
    // XXX: should use reflection to load level. This is just a test.
    std::shared_ptr<Level> level = std::make_shared<Level>();
    level->AddGameObject(test_mesh_go);
    level->AddGameObject(camera);
    nlohmann::json level_json;
    std::ifstream level_file(project_path / "assets" / "default_level.level.asset");
    level_file >> level_json;
    level_file.close();
    level->SetGUID(stringToGUID(level_json["guid"]));

    globalSystems.world->SetCurrentLevel(level);
    
    level->Load();
    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
