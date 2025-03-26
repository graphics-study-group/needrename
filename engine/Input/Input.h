#ifndef ENGINE_INPUT_INPUT_H
#define ENGINE_INPUT_INPUT_H

union SDL_Event;

namespace Engine
{
    class Input
    {
    public:
        Input() = default;
        virtual ~Input() = default;

        virtual void ProcessEvent(SDL_Event *event) = 0;
        virtual void Update(float dt) = 0;
    };
}

#endif // ENGINE_INPUT_INPUT_H
