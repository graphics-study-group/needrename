#ifndef EXAMPLE_EDITOR_RUN_GAME_EXAMPLE_CUSTOMCOMPONENT_H
#define EXAMPLE_EDITOR_RUN_GAME_EXAMPLE_CUSTOMCOMPONENT_H

#include <Framework/component/Component.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <memory>
#include <vector>

namespace Engine {
    class Component;
    class CameraComponent;
    class GameObject;
} // namespace Engine

using ObjectHandle = Engine::ObjectHandle;
using ComponentHandle = Engine::ComponentHandle;

class REFL_SER_CLASS(REFL_WHITELIST) SpinningComponent : public Engine::Component {
    REFL_SER_BODY(SpinningComponent)
public:
    REFL_ENABLE SpinningComponent(ObjectHandle gameObject);

    REFL_SER_ENABLE float m_speed = 30.0f;
    REFL_SER_ENABLE std::vector<float> no_use = {1.0f, 8.0f, 2.0f, 3.0f, 7.0f, 6.0f};
    REFL_SER_ENABLE enum class REFL_SER_CLASS() TestEnum {
        WOW, FURINA
    } no_use2 = TestEnum::WOW;

    REFL_ENABLE virtual void Init() override;
    REFL_ENABLE virtual void Tick() override;
};

class REFL_SER_CLASS(REFL_WHITELIST) ControlComponent : public Engine::Component {
    REFL_SER_BODY(ControlComponent)
public:
    REFL_ENABLE ControlComponent(ObjectHandle gameObject);

    REFL_SER_ENABLE std::shared_ptr<Engine::CameraComponent> m_camera{};
    REFL_SER_ENABLE float m_rotation_speed = 10.0f;
    REFL_SER_ENABLE float m_move_speed = 1.0f;
    REFL_SER_ENABLE float m_roll_speed = 1.0f;

    REFL_ENABLE virtual void Tick() override;
};

#endif // EXAMPLE_EDITOR_RUN_GAME_EXAMPLE_CUSTOMCOMPONENT_H
