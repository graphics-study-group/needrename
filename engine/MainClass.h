#ifndef ENGINE_MAINCLASS_INCLUDED
#define ENGINE_MAINCLASS_INCLUDED

#include "Core/Functional/OptionHandler.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <mutex>

namespace Engine {
    class RenderSystem;
    class WorldSystem;
    class AssetDatabase;
    class AssetManager;
    class GUISystem;
    class Input;
    class SDLWindow;
    class TimeSystem;
    class EventQueue;
    class ShaderCompiler;

    class MaterialLibrary;
    class ImageCubemapAsset;

    class MainClass {
    public:
        /**
         * @brief Obtain a shared pointer to the main class singleton.
         * 
         * @note Due to shared library unloading problems, the application
         * that calls this member must hold the returned shared pointer
         * until exit of main function.
         */
        [[nodiscard]]
        static std::shared_ptr<MainClass> GetInstance();

        MainClass() = default;
        virtual ~MainClass();

        void Initialize(
            const StartupOptions *opt,
            Uint32 sdl_init_flags,
            SDL_LogPriority = SDL_LOG_PRIORITY_INFO,
            Uint32 sdl_window_flags = 0
        );

        void LoadBuiltinAssets(const std::filesystem::path &path);
        void LoadProject(const std::filesystem::path &path);
        void MainLoop();
        void LoopFinite(uint64_t max_frame_count = 0u, float max_time_seconds = 0.0f);

        std::shared_ptr<SDLWindow> GetWindow() const;
        std::shared_ptr<TimeSystem> GetTimeSystem() const;
        std::shared_ptr<RenderSystem> GetRenderSystem() const;
        std::shared_ptr<WorldSystem> GetWorldSystem() const;
        std::shared_ptr<AssetDatabase> GetAssetDatabase() const;
        std::shared_ptr<AssetManager> GetAssetManager() const;
        std::shared_ptr<GUISystem> GetGUISystem() const;
        std::shared_ptr<Input> GetInputSystem() const;
        std::shared_ptr<EventQueue> GetEventQueue() const;
        std::shared_ptr<ShaderCompiler> GetShaderCompiler();

    protected:
        // XXX: window must destroyed before renderer. Because the window has some AllocatedImage2D. So the permutation
        // of renderer and window can not be changed.
        std::shared_ptr<RenderSystem> renderer{};
        std::shared_ptr<SDLWindow> window{};
        std::shared_ptr<TimeSystem> time{};
        std::shared_ptr<WorldSystem> world{};
        std::shared_ptr<AssetDatabase> asset_database{};
        std::shared_ptr<AssetManager> asset_manager{};
        std::shared_ptr<GUISystem> gui{};
        std::shared_ptr<Input> input{};
        std::shared_ptr<EventQueue> event_queue{};
        std::shared_ptr<ShaderCompiler> shader_compiler{};

        static std::weak_ptr <MainClass> m_instance;
        static std::once_flag m_instance_ready;

        bool m_on_quit = false;

        void RunOneFrame();

    private:
        // XXX: Temporary skybox material library
        std::shared_ptr<MaterialLibrary> m_skybox_material_library{};
        std::shared_ptr<ImageCubemapAsset> m_skybox_cubemap_asset{};
    };
} // namespace Engine

#endif // ENGINE_MAINCLASS_INCLUDED
