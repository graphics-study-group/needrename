#ifndef ENGINE_INPUT_GAMEPADINPUT_H
#define ENGINE_INPUT_GAMEPADINPUT_H

#include "Input.h"

namespace Engine
{
    class GamepadInput : public Input
    {
    public:
        GamepadInput() = default;
        virtual ~GamepadInput() = default;

        virtual void ProcessEvent(SDL_Event *event);
    };
}

#endif // ENGINE_INPUT_GAMEPADINPUT_H
