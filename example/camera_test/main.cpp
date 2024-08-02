#include <SDL3/SDL.h>
#include <cassert>

#include "consts.h"
#include "MainClass.h"
#include "Functional/SDLWindow.h"

#include "Render/RenderSystem.h"
#include "Framework/go/GameObject.h"
#include "Framework/level/Level.h"
#include "Framework/world/WorldSystem.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/component/RenderComponent/MeshComponent.h"

using namespace Engine;

class Camera : public GameObject
{
public:
    void Tick(float dt) override
    {
    }

    void Initialize(std::shared_ptr<RenderSystem> sys)
    {
        auto & transform = m_transformComponent->GetTransformRef();
        transform.SetPosition(glm::vec3(0.0f, -5.0f, 0.7f));

        std::shared_ptr <CameraComponent> cc = std::make_shared<CameraComponent>(weak_from_this());
        AddComponent(std::dynamic_pointer_cast<Component>(cc));
        sys->SetActiveCamera(cc);
    }
};

class MeshTest : public GameObject
{
public:
    void Tick(float) override
    {
    }

    void Initialize(MainClass * cmc, const char* path)
    {
        std::shared_ptr <MeshComponent> testMesh = 
            std::make_shared<MeshComponent>(weak_from_this());
        testMesh->ReadAndFlatten(path);
        AddComponent(testMesh);
        cmc->renderer->RegisterComponent(std::dynamic_pointer_cast<Engine::RendererComponent>(this->m_components[1]));
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

    std::shared_ptr<MeshTest> testMesh = std::make_shared<MeshTest>();
    testMesh->Initialize(cmc, "D:/testmesh/mesh.obj");
    cmc->world->current_level->AddGameObject(testMesh);

    std::shared_ptr<Camera> camera = std::make_shared<Camera>();
    camera->Initialize(cmc->renderer);
    cmc->world->current_level->AddGameObject(camera);

    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
