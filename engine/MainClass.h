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
    class AssetManager;
    class GUISystem;

    class MainClass
    {
    public:
        static std::shared_ptr<MainClass> GetInstance();

        MainClass() = default;
        virtual ~MainClass();

        void Initialize(const StartupOptions *opt, Uint32 sdl_init_flags, SDL_LogPriority = SDL_LOG_PRIORITY_INFO, Uint32 sdl_window_flags = 0);
        void MainLoop();

        std::shared_ptr<RenderSystem> GetRenderSystem() const;
        std::shared_ptr<WorldSystem> GetWorldSystem() const;
        std::shared_ptr<AssetManager> GetAssetManager() const;
        std::shared_ptr<GUISystem> GetGUISystem() const;

    protected:
        std::shared_ptr<SDLWindow> window{};
        std::shared_ptr<RenderSystem> renderer{};
        std::shared_ptr<WorldSystem> world{};
        std::shared_ptr<AssetManager> asset{};
        std::shared_ptr<GUISystem> gui{};
    };
}

#endif // MAINCLASS_H
