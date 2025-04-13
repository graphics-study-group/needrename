#ifndef ENGINE_INPUT_INPUT_H
#define ENGINE_INPUT_INPUT_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <Reflection/macros.h>
#include <Reflection/serialization_vector.h>
#include <Reflection/serialization_smart_pointer.h>
#include <SDL3/SDL_events.h>

namespace Engine
{
    class REFL_SER_CLASS(REFL_WHITELIST) Input
    {
        REFL_SER_BODY(Input)
    public:
        const static std::unordered_map<std::string, uint32_t> k_name_code_map;

    public:
        Input() = default;
        virtual ~Input() = default;

        virtual void ProcessEvent(SDL_Event *event);
        virtual void Update(float dt);

        enum AxisType
        {
            TypeKey,
            TypeMouseButton,
            TypeMouseMotion,
            TypeMouseWheel,
            TypeGamepadButton,
            TypeGamepadAxis
        };

        // Base class for virtual input axis
        class REFL_SER_CLASS(REFL_BLACKLIST) InputAxis
        {
            REFL_SER_BODY(InputAxis)
        public:
            InputAxis() = default;
            InputAxis(const std::string &name, AxisType type, float gravity = 3.0f, float dead = 0.001f, float sensitivity = 3.0f, bool snap = false, bool invert = false);
            virtual ~InputAxis() = default;

            std::string m_name{};
            AxisType m_type{};

            float m_gravity = 3.0f;     // Speed in units per second that the axis falls toward neutral when no input is present.
            float m_dead = 0.01f;      // How far the user needs to move an analog stick before your application registers the movement. At runtime, input from all analog devices that falls within this range will be considered null.
            float m_sensitivity = 3.0f; // Speed in units per second that the axis will move toward the target value. This is for digital devices only.
            bool m_snap = false;        // If enabled, the axis value will reset to zero when pressing a button that corresponds to the opposite direction.
            bool m_invert = false;

            SER_DISABLE float m_value = 0.0f;
            SER_DISABLE float m_smoothed_value = 0.0f;

            virtual float GetRawValue() const;
            virtual float GetValue() const;
            virtual void ProcessEvent(SDL_Event *event);
            virtual void UpdateSmoothedValue(float dt);
        };

        // ButtonAxis is a virtual axis that is controlled by a positive and negative button such as keyboard, mouse and gamepad button. It will be clamped to -1.0f or 1.0f.
        class REFL_SER_CLASS(REFL_BLACKLIST) ButtonAxis : public InputAxis
        {
            REFL_SER_BODY(ButtonAxis)
        public:
            ButtonAxis() = default;
            ButtonAxis(const std::string &name, AxisType type, const std::string &positive,  const std::string &negative, float gravity = 3.0f, float dead = 0.01f, float sensitivity = 3.0f, bool snap = false, bool invert = false);
            virtual ~ButtonAxis() = default;

            std::string m_positive{};
            std::string m_negative{};

            virtual void ProcessEvent(SDL_Event *event) override;
        };

        // MotionAxis is a virtual axis that will aggregate control signals during one frame. It is used for mouse movement and mouse wheel.
        class REFL_SER_CLASS(REFL_BLACKLIST) MotionAxis : public InputAxis
        {
            REFL_SER_BODY(MotionAxis)
        public:
            MotionAxis() = default;
            MotionAxis(const std::string &name, AxisType type, const std::string &axis, float motion_sensitivity = 1.0f, float gravity = 3.0f, float dead = 0.01f, float sensitivity = 3.0f, bool snap = false, bool invert = false);
            virtual ~MotionAxis() = default;

            std::string m_axis{};
            float m_motion_sensitivity = 1.0f;
            REFL_SER_DISABLE float m_sum_one_frame = 0.0f;

            virtual void ProcessEvent(SDL_Event *event) override;
            virtual void UpdateSmoothedValue(float dt) override;
        };

        // GamepadAxis is a virtual axis that is controlled by a gamepad axis or trigger.
        class REFL_SER_CLASS(REFL_BLACKLIST) GamepadAxis : public InputAxis
        {
            REFL_SER_BODY(GamepadAxis)
        public:
            GamepadAxis() = default;
            GamepadAxis(const std::string &name, AxisType type, const std::string &axis, float scale = 1.0f / 32768.0f, float gravity = 3.0f, float dead = 0.01f, float sensitivity = 3.0f, bool snap = false, bool invert = false);
            virtual ~GamepadAxis() = default;

            std::string m_axis{};
            float m_scale = 1.0f / 32768.0f;

            virtual void ProcessEvent(SDL_Event *event) override;
        };

        template <typename AxisType>
        void AddAxis(const AxisType &axis) { m_axes.push_back(std::make_shared<AxisType>(axis)); }
        virtual float GetAxis(const std::string &axis_name) const;
        virtual float GetAxisRaw(const std::string &axis_name) const;
        virtual void ResetAxes();

        REFL_SER_ENABLE std::vector<std::shared_ptr<InputAxis>> m_axes{};
    };
}

#endif // ENGINE_INPUT_INPUT_H
