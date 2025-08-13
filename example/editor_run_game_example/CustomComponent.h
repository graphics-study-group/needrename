#ifndef EXAMPLE_EDITOR_RUN_GAME_EXAMPLE_CUSTOMCOMPONENT_H
#define EXAMPLE_EDITOR_RUN_GAME_EXAMPLE_CUSTOMCOMPONENT_H

#include <Framework/component/Component.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <memory>

namespace Engine {
    class Component;
    class GameObject;
} // namespace Engine

class REFL_SER_CLASS(REFL_WHITELIST) SpinningComponent : public Engine::Component {
    // REFL_SER_BODY(SpinningComponent)
public:
    SpinningComponent(std::weak_ptr<Engine::GameObject> gameObject);

    REFL_SER_ENABLE float m_speed = 30.0f;

    REFL_ENABLE virtual void Init() override;
    REFL_ENABLE virtual void Tick() override;
};

class REFL_SER_CLASS(REFL_WHITELIST) ControlComponent : public Engine::Component {
    // REFL_SER_BODY(ControlComponent)
public:
    ControlComponent(std::weak_ptr<Engine::GameObject> gameObject);

    REFL_SER_ENABLE std::shared_ptr<Engine::CameraComponent> m_camera{};
    REFL_SER_ENABLE float m_rotation_speed = 10.0f;
    REFL_SER_ENABLE float m_move_speed = 1.0f;
    REFL_SER_ENABLE float m_roll_speed = 1.0f;

    REFL_ENABLE virtual void Tick() override;
};

#endif // EXAMPLE_EDITOR_RUN_GAME_EXAMPLE_CUSTOMCOMPONENT_H
