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
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Input/Input.h>
#include <Framework/component/Component.h>

using namespace Engine;

class ControlComponent : public Component
{
public:
    ControlComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {}

    std::shared_ptr<CameraComponent> m_camera;
    float m_rotation_speed = 10.0f;
    
    virtual void Tick(float dt) override
    {
        auto input = MainClass::GetInstance()->GetInputSystem();
        auto move_forward = input->GetAxis("move forward");
        auto move_left = input->GetAxis("move left");
        auto move_up = input->GetAxis("move up");
        auto look_button = input->GetAxisRaw("look button");
        auto look_x = input->GetAxisRaw("look x");
        auto look_y = input->GetAxisRaw("look y");

        if (look_button > 0.0f)
        {
            auto transform = m_parentGameObject.lock()->GetTransform();
            Transform delta_transform{};
            delta_transform.SetRotationEuler(glm::vec3{look_y * m_rotation_speed * dt, 0.0f, look_x * m_rotation_speed * dt});
            m_parentGameObject.lock()->SetTransform(delta_transform * transform);
        }
    }
};

int main(int argc, char **argv)
{
    int max_frame_count = std::numeric_limits<int>::max();
    if (argc > 1)
    {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0)
            return -1;
    }

    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Input Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO | SDL_INIT_GAMEPAD, SDL_LOG_PRIORITY_VERBOSE);
    cmc->GetAssetManager()->SetBuiltinAssetPath(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    cmc->GetAssetManager()->LoadBuiltinAssets();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    auto input = MainClass::GetInstance()->GetInputSystem();
    input->AddAxis(Input::ButtonAxis("move forward", Input::AxisType::TypeKey, "w", "s"));
    input->AddAxis(Input::ButtonAxis("move left", Input::AxisType::TypeKey, "a", "d"));
    input->AddAxis(Input::ButtonAxis("move up", Input::AxisType::TypeKey, "space", "left shift"));
    input->AddAxis(Input::ButtonAxis("look button", Input::AxisType::TypeMouseButton, "mouse right", ""));
    input->AddAxis(Input::MotionAxis("look x", Input::AxisType::TypeMouseMotion, "x", 1.0f, 3.0f, 0.001f, 3.0f, false, true));
    input->AddAxis(Input::MotionAxis("look y", Input::AxisType::TypeMouseMotion, "y", 1.0f, 3.0f, 0.001f, 3.0f, false, false));

    auto camera_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, 1.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    transform.SetScale({1.0f, 1.0f, 1.0f});
    camera_go->SetTransform(transform);
    auto camera_comp = camera_go->template AddComponent<CameraComponent>();
    camera_comp->set_aspect_ratio(1.0 * opt.resol_x / opt.resol_y);
    auto control_comp = camera_go->template AddComponent<ControlComponent>();
    control_comp->m_camera = camera_comp;
    cmc->GetRenderSystem()->SetActiveCamera(camera_comp);
    cmc->GetWorldSystem()->AddGameObjectToWorld(camera_go);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");
    cmc->LoopFiniteFrame(max_frame_count);

    return 0;
}
