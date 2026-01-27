#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Scene/GameObjectAsset.h>
#include <Framework/component/Component.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Framework/world/WorldSystem.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <UserInterface/Input.h>
#include <MainClass.h>
#include <Render/RenderSystem.h>
#include <Render/Renderer/Camera.h>
#include <cmake_config.h>

using namespace Engine;

class ControlComponent : public Component {
public:
    ControlComponent(ObjectHandle gameObject) : Component(gameObject) {
    }

    std::shared_ptr<CameraComponent> m_camera{};
    float m_rotation_speed = 10.0f;
    float m_move_speed = 1.0f;
    float m_roll_speed = 1.0f;

    virtual void Tick() override {
        auto input = MainClass::GetInstance()->GetInputSystem();
        auto move_forward = input->GetAxis("move forward");
        auto move_backward = input->GetAxis("move backward");
        auto move_right = input->GetAxis("move right");
        auto move_up = input->GetAxis("move up");
        auto roll_right = input->GetAxisRaw("roll right");
        auto look_x = input->GetAxisRaw("look x");
        auto look_y = input->GetAxisRaw("look y");
        Transform &transform = m_parentGameObject.lock()->GetTransformRef();
        float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
        transform.SetRotation(
            transform.GetRotation()
            * glm::quat(
                glm::vec3{
                    look_y * m_rotation_speed * dt, roll_right * m_roll_speed * dt, look_x * m_rotation_speed * dt
                }
            )
        );
        transform.SetPosition(
            transform.GetPosition()
            + transform.GetRotation() * glm::vec3{move_right, move_forward + move_backward, move_up} * m_move_speed * dt
        );
    }
};

int main(int argc, char **argv) {
    int max_frame_count = std::numeric_limits<int>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Input Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO | SDL_INIT_GAMEPAD, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    auto input = MainClass::GetInstance()->GetInputSystem();
    input->AddAxis(Input::ButtonAxis("move forward", Input::AxisType::TypeKey, "w", "s"));
    input->AddAxis(Input::ButtonAxis("move right", Input::AxisType::TypeKey, "d", "a"));
    input->AddAxis(Input::ButtonAxis("move up", Input::AxisType::TypeKey, "space", "left shift"));
    input->AddAxis(Input::ButtonAxis("roll right", Input::AxisType::TypeKey, "e", "q"));
    input->AddAxis(
        Input::MotionAxis("look x", Input::AxisType::TypeMouseMotion, "x", 1.0f, 3.0f, 0.001f, 3.0f, false, true)
    );
    input->AddAxis(
        Input::MotionAxis("look y", Input::AxisType::TypeMouseMotion, "y", 1.0f, 3.0f, 0.001f, 3.0f, false, true)
    );

    input->AddAxis(
        Input::GamepadAxis(
            "move up",
            Input::AxisType::TypeGamepadAxis,
            "gamepad axis left y",
            1.0f / 32768.0f,
            3.0f,
            0.01f,
            3.0f,
            false,
            true
        )
    );
    input->AddAxis(
        Input::GamepadAxis(
            "move right",
            Input::AxisType::TypeGamepadAxis,
            "gamepad axis left x",
            1.0f / 32768.0f,
            3.0f,
            0.01f,
            3.0f,
            false,
            false
        )
    );
    input->AddAxis(
        Input::GamepadAxis(
            "move forward",
            Input::AxisType::TypeGamepadAxis,
            "gamepad axis trigger right",
            1.0f / 32768.0f,
            3.0f,
            0.01f,
            3.0f,
            false,
            false
        )
    );
    input->AddAxis(
        Input::GamepadAxis(
            "move backward",
            Input::AxisType::TypeGamepadAxis,
            "gamepad axis trigger left",
            1.0f / 32768.0f,
            3.0f,
            0.01f,
            3.0f,
            false,
            true
        )
    );
    input->AddAxis(
        Input::GamepadAxis(
            "look x",
            Input::AxisType::TypeGamepadAxis,
            "gamepad axis right x",
            1.0f / 32768.0f * 0.2f,
            3.0f,
            0.01f,
            3.0f,
            false,
            true
        )
    );
    input->AddAxis(
        Input::GamepadAxis(
            "look y",
            Input::AxisType::TypeGamepadAxis,
            "gamepad axis right y",
            1.0f / 32768.0f * 0.2f,
            3.0f,
            0.01f,
            3.0f,
            false,
            true
        )
    );
    input->AddAxis(
        Input::ButtonAxis(
            "roll right", Input::AxisType::TypeGamepadButton, "gamepad right shoulder", "gamepad left shoulder"
        )
    );

    auto camera_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    Transform transform{};
    transform.SetPosition({0.0f, -0.7f, 0.5f});
    transform.SetRotationEuler(glm::vec3{glm::radians(-30.0f), 0.0, 0.0});
    transform.SetScale({1.0f, 1.0f, 1.0f});
    camera_go->SetTransform(transform);
    auto camera_comp = camera_go->template AddComponent<CameraComponent>();
    camera_comp->m_camera->set_aspect_ratio(1.0 * opt.resol_x / opt.resol_y);
    auto control_comp = camera_go->template AddComponent<ControlComponent>();
    control_comp->m_camera = camera_comp;
    cmc->GetWorldSystem()->SetActiveCamera(camera_comp->m_camera, &cmc->GetRenderSystem()->GetCameraManager());
    cmc->GetWorldSystem()->AddGameObjectToWorld(camera_go);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");
    cmc->LoopFinite(max_frame_count);

    return 0;
}
