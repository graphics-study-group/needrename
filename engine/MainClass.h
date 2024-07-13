#ifndef MAINCLASS_H
#define MAINCLASS_H

#include <memory>
#include "consts.h"
#include "Exception/exception.h"
#include "Functional/OptionHandler.h"
#include "Functional/SDLWindow.h"
#include "Render/RenderSystem.h"
#include "Framework/world/WorldSystem.h"

namespace Engine
{
    class MainClass
    {
    public:
        MainClass(Uint32, SDL_LogPriority = SDL_LOG_PRIORITY_INFO);
        virtual ~MainClass();

        void Initialize(const StartupOptions*);
        void MainLoop();

    private:
        Uint32 sdl_flags;
        std::shared_ptr<SDLWindow> window;
        std::shared_ptr<RenderSystem> renderer;
        std::shared_ptr<WorldSystem> world;
    };
}

#endif // MAINCLASS_H