#ifndef ENGINE_INPUT_KBMINPUT_H
#define ENGINE_INPUT_KBMINPUT_H

#include <glm.hpp>
#include <SDL3/SDL.h>
#include "Input.h"

namespace Engine
{
    class KBMInput : public Input
    {
    public:
        KBMInput() = default;
        virtual ~KBMInput() = default;

        virtual void ProcessEvent(SDL_Event *event) override;
        virtual void Update(float dt) override;

        bool GetKey(SDL_Scancode scancode) const;
        glm::vec2 GetMousePosition() const;
        bool GetMouseButton(int button) const;
        uint8_t GetMouseButtonClicks(int button) const;
    
    protected:
        bool m_key_down[SDL_SCANCODE_COUNT] = {false};
        bool m_mouse_button_down[10] = {false};
        uint8_t m_mouse_button_clicks[10] = {0};
        glm::vec2 m_mouse_position{0.0f, 0.0f};
        glm::vec2 m_mouse_last_position{-1.0f, -1.0f};
        float m_mouse_wheel_scroll{0.0f};
    };
}

#endif // ENGINE_INPUT_KBMINPUT_H
