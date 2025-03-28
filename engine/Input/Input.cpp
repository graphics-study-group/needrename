#include "Input.h"
#include <cassert>
#include <SDL3/SDL.h>

namespace Engine
{
    const std::unordered_map<std::string, uint32_t> Input::k_name_code_map =
        {
            {"a", SDL_SCANCODE_A},
            {"b", SDL_SCANCODE_B},
            {"c", SDL_SCANCODE_C},
            {"d", SDL_SCANCODE_D},
            {"e", SDL_SCANCODE_E},
            {"f", SDL_SCANCODE_F},
            {"g", SDL_SCANCODE_G},
            {"h", SDL_SCANCODE_H},
            {"i", SDL_SCANCODE_I},
            {"j", SDL_SCANCODE_J},
            {"k", SDL_SCANCODE_K},
            {"l", SDL_SCANCODE_L},
            {"m", SDL_SCANCODE_M},
            {"n", SDL_SCANCODE_N},
            {"o", SDL_SCANCODE_O},
            {"p", SDL_SCANCODE_P},
            {"q", SDL_SCANCODE_Q},
            {"r", SDL_SCANCODE_R},
            {"s", SDL_SCANCODE_S},
            {"t", SDL_SCANCODE_T},
            {"u", SDL_SCANCODE_U},
            {"v", SDL_SCANCODE_V},
            {"w", SDL_SCANCODE_W},
            {"x", SDL_SCANCODE_X},
            {"y", SDL_SCANCODE_Y},
            {"z", SDL_SCANCODE_Z},

            {"1", SDL_SCANCODE_1},
            {"2", SDL_SCANCODE_2},
            {"3", SDL_SCANCODE_3},
            {"4", SDL_SCANCODE_4},
            {"5", SDL_SCANCODE_5},
            {"6", SDL_SCANCODE_6},
            {"7", SDL_SCANCODE_7},
            {"8", SDL_SCANCODE_8},
            {"9", SDL_SCANCODE_9},
            {"0", SDL_SCANCODE_0},

            {"return", SDL_SCANCODE_RETURN},
            {"escape", SDL_SCANCODE_ESCAPE},
            {"backspace", SDL_SCANCODE_BACKSPACE},
            {"tab", SDL_SCANCODE_TAB},
            {"space", SDL_SCANCODE_SPACE},

            {"minus", SDL_SCANCODE_MINUS},
            {"equals", SDL_SCANCODE_EQUALS},
            {"left bracket", SDL_SCANCODE_LEFTBRACKET},
            {"right bracket", SDL_SCANCODE_RIGHTBRACKET},
            {"back slash", SDL_SCANCODE_BACKSLASH},
            // {"nonushash", SDL_SCANCODE_NONUSHASH}, // never use, see SDL_scancode.h
            {"semicolon", SDL_SCANCODE_SEMICOLON},
            {"apostrophe", SDL_SCANCODE_APOSTROPHE},
            {"grave", SDL_SCANCODE_GRAVE},
            {"comma", SDL_SCANCODE_COMMA},
            {"period", SDL_SCANCODE_PERIOD},
            {"slash", SDL_SCANCODE_SLASH},

            {"capslock", SDL_SCANCODE_CAPSLOCK},

            {"f1", SDL_SCANCODE_F1},
            {"f2", SDL_SCANCODE_F2},
            {"f3", SDL_SCANCODE_F3},
            {"f4", SDL_SCANCODE_F4},
            {"f5", SDL_SCANCODE_F5},
            {"f6", SDL_SCANCODE_F6},
            {"f7", SDL_SCANCODE_F7},
            {"f8", SDL_SCANCODE_F8},
            {"f9", SDL_SCANCODE_F9},
            {"f10", SDL_SCANCODE_F10},
            {"f11", SDL_SCANCODE_F11},
            {"f12", SDL_SCANCODE_F12},

            {"print screen", SDL_SCANCODE_PRINTSCREEN},
            {"scroll lock", SDL_SCANCODE_SCROLLLOCK},
            {"pause", SDL_SCANCODE_PAUSE},
            {"insert", SDL_SCANCODE_INSERT},
            {"home", SDL_SCANCODE_HOME},
            {"page up", SDL_SCANCODE_PAGEUP},
            {"delete", SDL_SCANCODE_DELETE},
            {"end", SDL_SCANCODE_END},
            {"page down", SDL_SCANCODE_PAGEDOWN},
            {"right", SDL_SCANCODE_RIGHT},
            {"left", SDL_SCANCODE_LEFT},
            {"down", SDL_SCANCODE_DOWN},
            {"up", SDL_SCANCODE_UP},

            {"num lock clear", SDL_SCANCODE_NUMLOCKCLEAR},
            {"kp divide", SDL_SCANCODE_KP_DIVIDE},
            {"kp multiply", SDL_SCANCODE_KP_MULTIPLY},
            {"kp minus", SDL_SCANCODE_KP_MINUS},
            {"kp plus", SDL_SCANCODE_KP_PLUS},
            {"kp enter", SDL_SCANCODE_KP_ENTER},
            {"kp 1", SDL_SCANCODE_KP_1},
            {"kp 2", SDL_SCANCODE_KP_2},
            {"kp 3", SDL_SCANCODE_KP_3},
            {"kp 4", SDL_SCANCODE_KP_4},
            {"kp 5", SDL_SCANCODE_KP_5},
            {"kp 6", SDL_SCANCODE_KP_6},
            {"kp 7", SDL_SCANCODE_KP_7},
            {"kp 8", SDL_SCANCODE_KP_8},
            {"kp 9", SDL_SCANCODE_KP_9},
            {"kp 0", SDL_SCANCODE_KP_0},
            {"kp period", SDL_SCANCODE_KP_PERIOD},

            {"left ctrl", SDL_SCANCODE_LCTRL},
            {"left shift", SDL_SCANCODE_LSHIFT},
            {"left alt", SDL_SCANCODE_LALT},
            {"left gui", SDL_SCANCODE_LGUI},
            {"right ctrl", SDL_SCANCODE_RCTRL},
            {"right shift", SDL_SCANCODE_RSHIFT},
            {"right alt", SDL_SCANCODE_RALT},
            {"right gui", SDL_SCANCODE_RGUI},

            {"mouse left", SDL_BUTTON_LEFT},
            {"mouse middle", SDL_BUTTON_MIDDLE},
            {"mouse right", SDL_BUTTON_RIGHT},
            {"mouse x1", SDL_BUTTON_X1},
            {"mouse x2", SDL_BUTTON_X2},

            {"gamepad south", SDL_GAMEPAD_BUTTON_SOUTH},
            {"gamepad east", SDL_GAMEPAD_BUTTON_EAST},
            {"gamepad west", SDL_GAMEPAD_BUTTON_WEST},
            {"gamepad north", SDL_GAMEPAD_BUTTON_NORTH},
            {"gamepad back", SDL_GAMEPAD_BUTTON_BACK},
            {"gamepad guide", SDL_GAMEPAD_BUTTON_GUIDE},
            {"gamepad start", SDL_GAMEPAD_BUTTON_START},
            {"gamepad left stick", SDL_GAMEPAD_BUTTON_LEFT_STICK},
            {"gamepad right stick", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
            {"gamepad left shoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
            {"gamepad right shoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
            {"gamepad dpad up", SDL_GAMEPAD_BUTTON_DPAD_UP},
            {"gamepad dpad down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
            {"gamepad dpad left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
            {"gamepad dpad right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
            {"gamepad misc1", SDL_GAMEPAD_BUTTON_MISC1},
            {"gamepad right paddle1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1},
            {"gamepad left paddle1", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1},
            {"gamepad right paddle2", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2},
            {"gamepad left paddle2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2},

            {"gamepad axis left x", SDL_GAMEPAD_AXIS_LEFTX},
            {"gamepad axis left y", SDL_GAMEPAD_AXIS_LEFTY},
            {"gamepad axis right x", SDL_GAMEPAD_AXIS_RIGHTX},
            {"gamepad axis right y", SDL_GAMEPAD_AXIS_RIGHTY},
            {"gamepad axis trigger left", SDL_GAMEPAD_AXIS_LEFT_TRIGGER},
            {"gamepad axis trigger right", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
    };

    Input::InputAxis::InputAxis(const std::string &name, AxisType type, float gravity, float dead, float sensitivity, bool snap, bool invert)
        : m_name(name), m_type(type), m_gravity(gravity), m_dead(dead), m_sensitivity(sensitivity), m_snap(snap), m_invert(invert)
    {
    }

    float Input::InputAxis::GetRawValue() const
    {
        return m_invert ? -m_value : m_value;
    }

    float Input::InputAxis::GetValue() const
    {
        return m_invert ? -m_smoothed_value : m_smoothed_value;
    }

    void Input::InputAxis::ProcessEvent(SDL_Event *)
    {
    }

    void Input::InputAxis::UpdateSmoothedValue(float dt)
    {
        m_smoothed_value = (m_snap && m_value * m_smoothed_value < 0.0f) ? 0.0f : m_smoothed_value;
        float dir = m_value > m_smoothed_value ? 1.0f : -1.0f;
        if (m_value == 0.0f)
            m_smoothed_value += dir * m_gravity * dt;
        else
            m_smoothed_value += dir * m_sensitivity * dt;
        if ((dir > 0.0f && m_smoothed_value > m_value) || (dir < 0.0f && m_smoothed_value < m_value))
            m_smoothed_value = m_value;
        if (-m_dead < m_smoothed_value && m_smoothed_value < m_dead)
            m_smoothed_value = 0.0f;
    }

    Input::ButtonAxis::ButtonAxis(const std::string &name, AxisType type, const std::string &positive, const std::string &negative, float gravity, float dead, float sensitivity, bool snap, bool invert)
        : InputAxis(name, type, gravity, dead, sensitivity, snap, invert), m_positive(positive), m_negative(negative)
    {
        assert(type == AxisType::TypeKey || type == AxisType::TypeMouseButton || type == AxisType::TypeGamepadButton);
    }

    void Input::ButtonAxis::ProcessEvent(SDL_Event *event)
    {
        if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
        {
            if (k_name_code_map.find(m_positive) != k_name_code_map.end() && event->key.scancode == k_name_code_map.at(m_positive))
                m_value += event->key.down ? 1.0f : -1.0f;
            if (k_name_code_map.find(m_negative) != k_name_code_map.end() && event->key.scancode == k_name_code_map.at(m_negative))
                m_value += event->key.down ? -1.0f : 1.0f;
            m_value = std::clamp(m_value, -1.0f, 1.0f);
        }
        else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            if (k_name_code_map.find(m_positive) != k_name_code_map.end() && event->button.button == k_name_code_map.at(m_positive))
                m_value += event->button.down ? 1.0f : -1.0f;
            if (k_name_code_map.find(m_negative) != k_name_code_map.end() && event->button.button == k_name_code_map.at(m_negative))
                m_value += event->button.down ? -1.0f : 1.0f;
            m_value = std::clamp(m_value, -1.0f, 1.0f);
        }
        else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event->type == SDL_EVENT_GAMEPAD_BUTTON_UP)
        {
            if (k_name_code_map.find(m_positive) != k_name_code_map.end() && event->button.button == k_name_code_map.at(m_positive))
                m_value += event->button.down ? 1.0f : -1.0f;
            if (k_name_code_map.find(m_negative) != k_name_code_map.end() && event->button.button == k_name_code_map.at(m_negative))
                m_value += event->button.down ? -1.0f : 1.0f;
            m_value = std::clamp(m_value, -1.0f, 1.0f);
        }
    }

    Input::MotionAxis::MotionAxis(const std::string &name, AxisType type, const std::string &axis, float motion_sensitivity, float gravity, float dead, float sensitivity, bool snap, bool invert)
        : InputAxis(name, type, gravity, dead, sensitivity, snap, invert), m_axis(axis), m_motion_sensitivity(motion_sensitivity)
    {
        assert(type == AxisType::TypeMouseMotion || type == AxisType::TypeMouseWheel);
    }

    void Input::MotionAxis::ProcessEvent(SDL_Event *event)
    {
        if (event->type == SDL_EVENT_MOUSE_MOTION)
        {
            if (m_axis == "x")
                m_sum_one_frame += event->motion.xrel;
            else if (m_axis == "y")
                m_sum_one_frame += event->motion.yrel;
        }
        if (event->type == SDL_EVENT_MOUSE_WHEEL)
        {
            if (m_axis == "x")
                m_sum_one_frame += event->wheel.x;
            else if (m_axis == "y")
                m_sum_one_frame += event->wheel.y;
        }
    }

    void Input::MotionAxis::UpdateSmoothedValue(float dt)
    {
        m_value = m_sum_one_frame * m_motion_sensitivity;
        m_sum_one_frame = 0.0f;
        InputAxis::UpdateSmoothedValue(dt);
    }

    Input::GamepadAxis::GamepadAxis(const std::string &name, AxisType type, const std::string &axis, float scale, float gravity, float dead, float sensitivity, bool snap, bool invert)
        : InputAxis(name, type, gravity, dead, sensitivity, snap, invert), m_axis(axis), m_scale(scale)
    {
        assert(type == AxisType::TypeGamepadAxis);
    }

    void Input::GamepadAxis::ProcessEvent(SDL_Event *event)
    {
        if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
        {
            if (k_name_code_map.find(m_axis) != k_name_code_map.end() && event->gaxis.axis == k_name_code_map.at(m_axis))
                m_value = event->gaxis.value * m_scale;
        }
    }

    void Input::ProcessEvent(SDL_Event *event)
    {
        for (auto &axis : m_axes)
        {
            axis->ProcessEvent(event);
        }
    }

    void Input::Update(float dt)
    {
        for (auto &axis : m_axes)
        {
            axis->UpdateSmoothedValue(dt);
        }
    }

    float Input::GetAxis(const std::string &axis_name) const
    {
        float result = 0.0f;
        for (auto &axis : m_axes)
        {
            if (axis->m_name == axis_name)
            {
                float temp = axis->GetValue();
                if (std::abs(temp) > std::abs(result))
                    result = temp;
            }
        }
        return result;
    }

    float Input::GetAxisRaw(const std::string &axis_name) const
    {
        float result = 0.0f;
        for (auto &axis : m_axes)
        {
            if (axis->m_name == axis_name)
            {
                float temp = axis->GetRawValue();
                if (std::abs(temp) > std::abs(result))
                    result = temp;
            }
        }
        return result;
    }

    void Input::ResetAxes()
    {
        for (auto &axis : m_axes)
        {
            axis->m_value = 0.0f;
            axis->m_smoothed_value = 0.0f;
        }
    }
}
