#ifndef ENGINE_INPUT_KBMINPUT_H
#define ENGINE_INPUT_KBMINPUT_H

#include "Input.h"

namespace Engine
{
    class KBMInput : public Input
    {
    public:
        KBMInput() = default;
        virtual ~KBMInput() = default;

        virtual void ProcessEvent(SDL_Event *event);
    };
}

#endif // ENGINE_INPUT_KBMINPUT_H
