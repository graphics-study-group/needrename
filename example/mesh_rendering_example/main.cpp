#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>

#include "cmake_config.h"
#include "consts.h"
#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Render/RenderSystem.h"
#include "Framework/go/GameObject.h"
#include "Framework/level/Level.h"
#include "Framework/world/WorldSystem.h"
#include "Framework/component/RenderComponent/MeshComponent.h"

using namespace Engine;

class MeshTest : public GameObject
{
public:
    void Tick(float dt) override
    {
        auto & transform = m_transformComponent->GetTransformRef();
        transform.SetRotation(glm::quat(glm::vec3(0.0f, glm::radians(30.0f) * dt, 0.0f)) * transform.GetRotation());

        // Note: The translation of euler is not consistent. so this code will not work.
        // glm::vec3 euler = m_transformComponent->GetEulerAngles();
        // euler.y += glm::radians(30.0f);
        // m_transformComponent->SetEulerAngles(euler);
    }

    void Initialize(MainClass * cmc, const char* path)
    {
        auto & transform = m_transformComponent->GetTransformRef();
        transform
            .SetPosition(glm::vec3(0.0f, -0.9f, 0.0f))
            .SetRotationEuler(glm::vec3(0.0f, glm::radians(180.0f), 0.0f));
        std::shared_ptr <MeshComponent> testMesh = 
            std::make_shared<MeshComponent>(weak_from_this());
        testMesh->ReadAndFlatten(path);
        AddComponent(testMesh);
        globalSystems.renderer->RegisterComponent(std::dynamic_pointer_cast<Engine::RendererComponent>(this->m_components[1]));
    }
};

class MeshTest2 : public GameObject
{
public:
    void Tick(float dt) override
    {
        auto & transform = m_transformComponent->GetTransformRef();
        transform.SetRotation(glm::quat(glm::vec3(glm::radians(60.f) * dt, 0.0f, 0.0f)) * transform.GetRotation());
    }

    void Initialize(MainClass * cmc, const char* path)
    {
        auto & transform = m_transformComponent->GetTransformRef();
        transform.SetPosition(glm::vec3(0.3f, 0.6f, 0.0f))
                 .SetRotationEuler(glm::vec3(0.0f, glm::radians(180.0f), 0.0f))
                 .SetScale(glm::vec3(0.5f, 0.5f, 0.5f));
        std::shared_ptr <MeshComponent> testMesh = 
            std::make_shared<MeshComponent>(weak_from_this());
        testMesh->ReadAndFlatten(path);
        AddComponent(testMesh);
        globalSystems.renderer->RegisterComponent(std::dynamic_pointer_cast<Engine::RendererComponent>(this->m_components[1]));
    }
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

    std::filesystem::path mesh_path(ENGINE_ASSETS_DIR);
    mesh_path = mesh_path / "__noupload" / "keqing" / "mesh.obj";
    std::shared_ptr<MeshTest> testMesh = std::make_shared<MeshTest>();
    testMesh->Initialize(cmc, mesh_path.string().c_str());
    std::shared_ptr<MeshTest2> testMesh2 = std::make_shared<MeshTest2>();
    testMesh2->Initialize(cmc, mesh_path.string().c_str());
    testMesh2->m_parentGameObject = testMesh;
    globalSystems.world->current_level->AddGameObject(testMesh);
    globalSystems.world->current_level->AddGameObject(testMesh2);

    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
