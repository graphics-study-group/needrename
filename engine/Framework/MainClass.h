#ifndef FRAMEWORK_MAINCLASS_INCLUDED
#define FRAMEWORK_MAINCLASS_INCLUDED

#include "Core/Functional/OptionHandler.h"
#include "Framework/framework_export.h"
#include "Rhi/Device/DeviceContext.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace Engine {
    class RenderSystem;
    class WorldSystem;
    class PhysicsSystem;
    class AssetDatabase;
    class AssetManager;
    class GUISystem;
    class Input;
    class SDLWindow;
    class TimeSystem;
    class ShaderCompiler;

    class ComplexRenderGraphBuilder;
    class RenderGraph;
    enum class RGTextureHandle : int32_t;
    struct XpbdConfig;

    class FRAMEWORK_API MainClass {
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

        MainClass();
        virtual ~MainClass();

        void Initialize(
            const StartupOptions *opt,
            Uint32 sdl_init_flags,
            SDL_LogPriority = SDL_LOG_PRIORITY_INFO,
            Uint32 sdl_window_flags = 0
        );

        void LoadBuiltinAssets(const std::filesystem::path &path);
        /**
         * @brief Load a project and register the default physics solver.
         *
         * @param path Project directory.
         * @param solver_config Optional XPBD solver configuration applied at
         *        solver creation. Null keeps the engine defaults. This is a
         *        temporary seam until project-level configuration exists: pass
         *        the config once here, do not add per-field overrides.
         */
        void LoadProject(const std::filesystem::path &path, const XpbdConfig *solver_config = nullptr);
        void MainLoop();
        void LoopFinite(uint64_t max_frame_count = 0u, float max_time_seconds = 0.0f);

        std::shared_ptr<SDLWindow> GetWindow() const;
        std::shared_ptr<TimeSystem> GetTimeSystem() const;
        std::shared_ptr<RenderSystem> GetRenderSystem() const;
        std::shared_ptr<WorldSystem> GetWorldSystem() const;
        std::shared_ptr<PhysicsSystem> GetPhysicsSystem() const;
        std::shared_ptr<AssetDatabase> GetAssetDatabase() const;
        std::shared_ptr<AssetManager> GetAssetManager() const;
        std::shared_ptr<GUISystem> GetGUISystem() const;
        std::shared_ptr<Input> GetInputSystem() const;
        std::shared_ptr<ShaderCompiler> GetShaderCompiler() const;

        void SetRenderGraph(std::unique_ptr<RenderGraph> render_graph, RGTextureHandle final_color_attachment_id);

        /// @brief Parts of the frame a diagnostic run may leave out.
        enum class DiagnosticSkip : uint32_t {
            None = 0,
            /// @brief Skip the physics solvers' compute passes.
            Physics = 1u << 0,
            /// @brief Skip the model-matrix compute dispatch.
            ModelMatrices = 1u << 1,
            /// @brief Skip the render graph's passes.
            Passes = 1u << 2,
            /// @brief Skip the shadow map passes only.
            ShadowPasses = 1u << 3,
            /// @brief Skip the compute passes only.
            ComputePasses = 1u << 4,
            /// @brief Skip the draws inside the shadow map passes, keeping their clears.
            ShadowDraws = 1u << 5,
            /// @brief Skip the main lit pass.
            LitPass = 1u << 6
        };

        /**
         * @brief Leave part of the frame out, for bisecting a fault that only one runtime shows.
         *
         * The browser has no environment to set before the page starts, so the switches are
         * stated here rather than read from `getenv` alone: an example parses them from its
         * arguments, and a desktop run can also set the matching `NEEDRENAME_SKIP_*` variable.
         *
         * @param skip The parts to leave out, or `None` to record everything.
         */
        static void SetDiagnosticSkip(DiagnosticSkip skip) noexcept;

        /// @brief What the last @ref SetDiagnosticSkip left out.
        static DiagnosticSkip GetDiagnosticSkip() noexcept;

    protected:
        std::unique_ptr<Rhi::DeviceContext> m_device_context{};
        std::shared_ptr<RenderSystem> renderer{};
        std::shared_ptr<SDLWindow> window{};
        std::shared_ptr<TimeSystem> time{};
        std::shared_ptr<PhysicsSystem> physics{};
        std::shared_ptr<WorldSystem> world{};
        std::shared_ptr<AssetDatabase> asset_database{};
        std::shared_ptr<AssetManager> asset_manager{};
        std::shared_ptr<GUISystem> gui{};
        std::shared_ptr<Input> input{};
        std::shared_ptr<ShaderCompiler> shader_compiler{};

        std::unique_ptr<RenderGraph> render_graph{};
        RGTextureHandle m_final_color_attachment_id{0};

        static std::weak_ptr<MainClass> m_instance;
        static std::once_flag m_instance_ready;

        bool m_on_quit = false;

        uint32_t m_last_present_width{0};
        uint32_t m_last_present_height{0};

        void RunOneFrame();

        /// @brief The skips stated through @ref SetDiagnosticSkip, as a bit mask.
        static inline uint32_t diagnostic_skip{0};

    };
} // namespace Engine

#endif // FRAMEWORK_MAINCLASS_INCLUDED