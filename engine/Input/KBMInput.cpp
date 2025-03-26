#include "KBMInput.h"
#include <SDL3/SDL.h>

namespace Engine
{
    void KBMInput::ProcessEvent(SDL_Event *event)
    {
        switch (event->type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            m_key_down[event->key.scancode] = event->key.down;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Key %s: scancode=%d, keycode=%d, modifiers=%d, repeat=%d",
                        event->key.down ? "pressed" : "released",
                        event->key.scancode,
                        event->key.key,
                        event->key.mod,
                        event->key.repeat);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            m_mouse_position = glm::vec2(event->motion.x, event->motion.y);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Mouse motion: x=%f, y=%f, xrel=%f, yrel=%f",
                        event->motion.x,
                        event->motion.y,
                        event->motion.xrel,
                        event->motion.yrel);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            m_mouse_button_down[event->button.button] = event->button.down;
            m_mouse_button_clicks[event->button.button] = event->button.clicks;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Mouse button %s: button=%d, clicks=%d, x=%f, y=%f",
                        event->button.down ? "pressed" : "released",
                        event->button.button,
                        event->button.clicks,
                        event->button.x,
                        event->button.y);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            m_mouse_wheel_scroll += event->wheel.y;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Mouse wheel: x=%f, y=%f",
                        event->wheel.x,
                        event->wheel.y);
            break;
        }
    }

    void KBMInput::Update(float)
    {
        m_mouse_last_position = m_mouse_position;
        m_mouse_wheel_scroll = 0.0f;
    }
}
