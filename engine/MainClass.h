#ifndef MAINCLASS_H
#define MAINCLASS_H

#include "Exception/exception.h"
#include "Core/Functional/OptionHandler.h"
#include "consts.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace Engine {
    class RenderSystem;
    class WorldSystem;
    class AssetManager;
    class GUISystem;
    class Input;
    class SDLWindow;
    class TimeSystem;
    class EventQueue;

    class MainClass {
    public:
        static std::shared_ptr<MainClass> GetInstance();

        MainClass() = default;
        virtual ~MainClass();

        void Initialize(
            const StartupOptions *opt,
            Uint32 sdl_init_flags,
            SDL_LogPriority = SDL_LOG_PRIORITY_INFO,
            Uint32 sdl_window_flags = 0
        );
        void LoadProject(const std::filesystem::path &path);
        void MainLoop();
        void LoopFinite(uint64_t max_frame_count = 0u, float max_time_seconds = 0.0f);

        std::shared_ptr<SDLWindow> GetWindow() const;
        std::shared_ptr<TimeSystem> GetTimeSystem() const;
        std::shared_ptr<RenderSystem> GetRenderSystem() const;
        std::shared_ptr<WorldSystem> GetWorldSystem() const;
        std::shared_ptr<AssetManager> GetAssetManager() const;
        std::shared_ptr<GUISystem> GetGUISystem() const;
        std::shared_ptr<Input> GetInputSystem() const;
        std::shared_ptr<EventQueue> GetEventQueue() const;

    protected:
        // XXX: window must destroyed before renderer. Because the window has some AllocatedImage2D. So the permutation
        // of renderer and window can not be changed.
        std::shared_ptr<RenderSystem> renderer{};
        std::shared_ptr<SDLWindow> window{};
        std::shared_ptr<TimeSystem> time{};
        std::shared_ptr<WorldSystem> world{};
        std::shared_ptr<AssetManager> asset{};
        std::shared_ptr<GUISystem> gui{};
        std::shared_ptr<Input> input{};
        std::shared_ptr<EventQueue> event_queue{};

        bool m_on_quit = false;

        void RunOneFrame();
    };
} // namespace Engine

#endif // MAINCLASS_H
