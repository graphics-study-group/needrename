#ifndef MAINCLASS_H
#define MAINCLASS_H

#include <memory>
#include "consts.h"
#include "Exception/exception.h"
#include "Functional/OptionHandler.h"
#include "Functional/SDLWindow.h"

namespace Engine
{
    class RenderSystem;
    class WorldSystem;

    class MainClass
    {
    public:
        MainClass(Uint32, SDL_LogPriority = SDL_LOG_PRIORITY_INFO);
        virtual ~MainClass();

        void Initialize(const StartupOptions*, Uint32 = 0);
        void MainLoop();

    protected:
        std::shared_ptr <SDLWindow> window {};
        std::shared_ptr <RenderSystem> renderer {};
        std::shared_ptr <WorldSystem> world {};
    };
}

#endif // MAINCLASS_H