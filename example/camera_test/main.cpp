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
        if(orbit_angle > 2 * pi)
            orbit_angle -= 2 * pi;
        orbit_angle += dt / 2.0;
        auto & transform = m_transformComponent->GetTransformRef();
        glm::vec3 pos, rot;
        pos = glm::vec3(radius * glm::sin(orbit_angle), radius * glm::cos(orbit_angle), 0.7f);
        rot = glm::vec3(0.0f, 0.0f, pi - orbit_angle);
        transform.SetPosition(pos).SetRotationEuler(rot);

        SDL_LogInfo(0, "Rotation angle: %f, delta-t: %f", glm::degrees(orbit_angle), dt);
    }

    void Initialize(std::shared_ptr<RenderSystem> sys)
    {
        auto & transform = m_transformComponent->GetTransformRef();
        glm::vec3 pos, rot;
        pos = glm::vec3(radius * glm::sin(orbit_angle), radius * glm::cos(orbit_angle), 0.7f);
        rot = glm::vec3(0.0f, 0.0f, pi - orbit_angle);
        transform.SetPosition(pos).SetRotationEuler(rot);

        std::shared_ptr <CameraComponent> cc = std::make_shared<CameraComponent>(weak_from_this());
        AddComponent(std::dynamic_pointer_cast<Component>(cc));
        sys->SetActiveCamera(cc);
    }

protected:
    float pi {glm::pi<float>()};
    float orbit_angle {0.0f};
    float radius{5.0f};

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

    std::shared_ptr<MeshTest> testMesh = std::make_shared<MeshTest>();
    testMesh->Initialize(cmc, "E:/CaptainChen/Projects/MyCodes/game/assets/__noupload/keqing/mesh.obj");
    globalSystems.world->current_level->AddGameObject(testMesh);

    std::shared_ptr<Camera> camera = std::make_shared<Camera>();
    camera->Initialize(globalSystems.renderer);
    globalSystems.world->current_level->AddGameObject(camera);

    cmc->MainLoop();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
    delete opt;
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    delete cmc;

    return 0;
}
