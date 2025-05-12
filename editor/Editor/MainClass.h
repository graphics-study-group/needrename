#ifndef EDITOR_MAINCLASS_INCLUDED
#define EDITOR_MAINCLASS_INCLUDED

#include <vector>
#include <memory>
#include <filesystem>
#include <Functional/OptionHandler.h>
#include <Functional/SDLWindow.h>

namespace Editor
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

        std::shared_ptr<SDLWindow> GetWindow() const;
        std::shared_ptr<RenderSystem> GetRenderSystem() const;
        std::shared_ptr<WorldSystem> GetWorldSystem() const;
        std::shared_ptr<AssetManager> GetAssetManager() const;
        std::shared_ptr<GUISystem> GetGUISystem() const;
        // std::shared_ptr<Input> GetInputSystem() const;

    protected:
        std::shared_ptr<SDLWindow> window{};
        std::shared_ptr<RenderSystem> renderer{};
        std::shared_ptr<WorldSystem> world{};
        std::shared_ptr<AssetManager> asset{};
        std::shared_ptr<GUISystem> gui{};
        // std::shared_ptr<Input> input{};

        bool m_on_quit = false;

        void RunOneFrame(float dt);
    };
}

#endif // EDITOR_MAINCLASS_INCLUDED
