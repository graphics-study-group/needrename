#ifndef MAINCLASS_H
#define MAINCLASS_H

#include <vector>
#include <memory>
#include <filesystem>
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
    class Input;

    class MainClass
    {
    public:
        static std::shared_ptr<MainClass> GetInstance();

        MainClass() = default;
        virtual ~MainClass();

        void Initialize(const StartupOptions *opt, Uint32 sdl_init_flags, SDL_LogPriority = SDL_LOG_PRIORITY_INFO, Uint32 sdl_window_flags = 0);
        void LoadProject(const std::filesystem::path &path);
        void MainLoop();
        void LoopFiniteFrame(int max_frame_count);
        void LoopFiniteTime(float max_time);

        std::shared_ptr<SDLWindow> GetWindow() const;
        std::shared_ptr<RenderSystem> GetRenderSystem() const;
        std::shared_ptr<WorldSystem> GetWorldSystem() const;
        std::shared_ptr<AssetManager> GetAssetManager() const;
        // std::shared_ptr<GUISystem> GetGUISystem() const;
        std::shared_ptr<Input> GetInputSystem() const;

    protected:
        // XXX: window must destroyed before renderer. Because the window has some AllocatedImage2D. So the permutation of renderer and window can not be changed. 
        std::shared_ptr<RenderSystem> renderer{};
        std::shared_ptr<SDLWindow> window{};

        std::shared_ptr<WorldSystem> world{};
        std::shared_ptr<AssetManager> asset{};
        // std::shared_ptr<GUISystem> gui{};
        std::shared_ptr<Input> input{};

        bool m_on_quit = false;

        void RunOneFrame(float dt);
    };
}

#endif // MAINCLASS_H
