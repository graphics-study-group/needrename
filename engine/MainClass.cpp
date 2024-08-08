#include "MainClass.h"

#include "Framework/world/WorldSystem.h"
#include "Render/RenderSystem.h"
#include "Asset/AssetManager/AssetManager.h"

#include <exception>
#include <glad/glad.h>

namespace Engine
{
    MainClass::MainClass(Uint32 flags, SDL_LogPriority logPrior)
    {
        this->sdl_flags = flags;
        if (SDL_Init(flags) < 0)
            throw Exception::SDLExceptions::cant_init();
        SDL_SetLogPriorities(logPrior);
        globalSystems.window = nullptr;
    }

    MainClass::~MainClass()
    {
        SDL_Quit();
    }

    void MainClass::Initialize(const StartupOptions *opt)
    {
        if (opt->instantQuit)
            return;
        globalSystems.window = std::make_shared<SDLWindow>(
            opt->title.c_str(), 
            opt->resol_x, 
            opt->resol_y, 
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
            );
        globalSystems.window->CreateRenderer();

        globalSystems.renderer = std::make_shared<RenderSystem>();
        globalSystems.world = std::make_shared<WorldSystem>();
        globalSystems.assetManager = std::make_shared<AssetManager>();
    }

    void MainClass::MainLoop()
    {
        SDL_Event event;
        bool onQuit = false;

        unsigned int FPS_TIMER = 0;

        while (!onQuit)
        {
            float current_time = SDL_GetTicks();
            float dt = (current_time - FPS_TIMER) / 1000.0f;
            
            globalSystems.window->BeforeEventLoop(); // ???
            globalSystems.world->Tick(dt);

            // FIXME: Viewport infomation should be pass to Render() by context and camera
            // instead of being set here.
            auto pWindow = globalSystems.window->GetWindow();
            int w, h;
            SDL_GetWindowSizeInPixels(pWindow, &w, &h);
            glViewport(0, 0, w, h);
            
            globalSystems.renderer->Render();

            globalSystems.window->AfterEventLoop();

            // TODO: write a control system instead of using this window event
            try
            {
                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_EVENT_QUIT) {
                        onQuit = true;
                        break;
                    }
                    // if (!this->window->dispatchEvents(event))
                    // {
                    //     onQuit = true;
                    //     SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Exited after onEvent()");
                    // }
                }
            }
            catch (std::exception &except)
            {
                fprintf(stderr, "%s", except.what());
                // Do a forced redraw to print error messages
                globalSystems.window->OnDrawOverall(true);
                throw;
            }

            current_time = SDL_GetTicks();
            if (current_time - FPS_TIMER < TPF_LIMIT)
                SDL_Delay(TPF_LIMIT - current_time + FPS_TIMER);
            FPS_TIMER = current_time;
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
    }
}