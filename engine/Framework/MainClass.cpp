#include "Framework/MainClass.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRuntime.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <Framework/Input/Input.h>
#include <Framework/Scene/LevelAsset.h>
#include <Framework/World/EventQueue.h>
#include <Framework/World/WorldSystem.h>
#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSystem.h>
#include <Physics/Solver/XPBDGpuSolver.h>
#include <Render/FullRenderSystem.h>
#include <Render/Material/MaterialAsset.h>
#include <Render/RenderRuntime.h>
#include <Render/Shader/ShaderCompiler.h>
#include <Render/UserInterface/GUISystem.h>

#include <exception>
#include <fstream>
#include <glslang/Public/ShaderLang.h>
#include <nlohmann/json.hpp>

extern "C"
{
    void RegisterCoreTypes();
    void RegisterRhiTypes();
    void RegisterAssetCoreTypes();
    void RegisterPhysicsTypes();
    void RegisterRenderTypes();
    void RegisterFrameworkTypes();
}

namespace Engine {
    std::weak_ptr<MainClass> MainClass::m_instance;
    std::once_flag MainClass::m_instance_ready{};

    std::shared_ptr<MainClass> MainClass::GetInstance() {
        // XXX: Check thread safety!
        if (!m_instance.expired()) {
            return m_instance.lock();
        }

        std::shared_ptr<MainClass> sptr{nullptr};
        std::call_once(MainClass::m_instance_ready, [&] {
            sptr = std::make_shared<MainClass>();
            MainClass::m_instance = sptr;
        });
        return sptr;
    }

    MainClass::~MainClass() {
        SetRenderRuntime({});
        SetAssetRuntime({});
        SDL_Quit();
    }

    void MainClass::LoadBuiltinAssets(const std::filesystem::path &path) {
        // XXX: Modify here when multiple AssetDatabase types are supported
        std::dynamic_pointer_cast<FileSystemDatabase>(this->asset_database)->LoadBuiltinAssets(path);
    }

    void MainClass::LoadProject(const std::filesystem::path &path) {
        std::dynamic_pointer_cast<FileSystemDatabase>(this->asset_database)->LoadProjectAssets(path / "assets");

        nlohmann::json project_config;
        std::ifstream file(path / "project.config");
        if (file.is_open()) {
            project_config = nlohmann::json::parse(file);
            file.close();
        } else {
            throw std::runtime_error("Cannot open project.config");
        }
        assert(project_config.contains("default_level"));
        GUID default_level_guid(project_config["default_level"].get<std::string>());
        auto level_asset = dynamic_cast<LevelAsset *>(this->asset_manager->LoadAssetImmediately(default_level_guid));
        level_asset->LoadToWorld();

        auto *phys_scene = world->GetMainSceneRef().GetPhysicsScene();
        auto solver = std::make_unique<XpbdGpuSolver>(*this->m_device_context);
        XpbdConfig config{};
        config.gravity = glm::vec3(0.0f, 0.0f, -9.81f);
        config.time_step = 1.0f / 60.0f;
        config.num_substep_perstep = 2;
        config.num_iter_persubstep = 50;
        config.num_velocity_iters = 10;
        config.max_contact_points = 50000u;
        config.contact_margin = 0.001f;
        config.grid_cell_size = 2.0f;
        config.grid_world_min = glm::vec3(-100.0f, -100.0f, -5.0f);
        config.grid_world_max = glm::vec3(100.0f, 100.0f, 50.0f);
        config.max_cells_per_shape = 8;
        config.max_global_shape_count = 128;
        config.fallback_all_pairs_threshold = 8;
        solver->SetConfig(config);
        physics->RegisterSolver(phys_scene->GetSceneID(), std::move(solver));
    }

    void MainClass::Initialize(
        const StartupOptions *opt, Uint32 sdl_init_flags, SDL_LogPriority sdl_logPrior, Uint32 sdl_window_flags
    ) {
        if (!SDL_Init(sdl_init_flags)) throw std::runtime_error("Cannot initialize SDL systems.");
        SDL_SetLogPriorities(sdl_logPrior);
        this->window = nullptr;

        if (sdl_window_flags == 0)
            sdl_window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (opt->instantQuit) return;

        const bool is_headless = opt->headless;
        if (!is_headless) {
            this->window =
                std::make_shared<SDLWindow>(opt->title.c_str(), opt->resol_x, opt->resol_y, sdl_window_flags);
        }
        this->time = std::make_shared<TimeSystem>();
        Rhi::DeviceInterface::DeviceConfiguration cfg{
            .window = is_headless ? nullptr : this->window->GetWindow(),
            .application_name = opt->title.c_str(),
            .application_version = 0,
            .dynamic_dispatcher = nullptr,
        };
        this->m_device_context = std::make_unique<Rhi::DeviceContext>(cfg);
        this->renderer = std::make_shared<RenderSystem>(this->window, *this->m_device_context);
        this->physics = std::make_shared<PhysicsSystem>();
        this->world = std::make_shared<WorldSystem>();
        this->asset_database = std::make_shared<FileSystemDatabase>();
        this->asset_manager = std::make_shared<AssetManager>();
        SetAssetRuntime({asset_manager.get(), asset_database.get()});
        if (!is_headless) {
            this->gui = std::make_shared<GUISystem>();
            this->input = std::make_shared<Input>();
        }

        this->renderer->Create();
        if (!is_headless) {
            this->gui->Create(this->window->GetWindow());
        }
        AnnoRefl::Initialize();
        RegisterCoreTypes();
        RegisterRhiTypes();
        RegisterAssetCoreTypes();
        RegisterPhysicsTypes();
        RegisterRenderTypes();
        RegisterFrameworkTypes();

        // if in editor mode
        auto *fs_db = std::dynamic_pointer_cast<FileSystemDatabase>(this->asset_database).get();
        this->shader_compiler = std::make_shared<ShaderCompiler>(fs_db->GetProjectAssetsPath());
        SetRenderRuntime({renderer.get(), shader_compiler.get()});
    }

    void MainClass::MainLoop() {
        while (!m_on_quit) {
            this->time->NextFrame();
            this->RunOneFrame();
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
        renderer->WaitForIdle();
    }

    void MainClass::LoopFinite(uint64_t max_frame_count, float max_time_seconds) {
        while (!m_on_quit) {
            this->time->NextFrame();
            this->RunOneFrame();
            if (max_frame_count > 0 && this->time->GetFrameCount() >= max_frame_count) break;
            if (max_time_seconds > 0.0f && this->time->GetDeltaTimeInSeconds() >= max_time_seconds) break;
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
        renderer->WaitForIdle();
    }

    std::shared_ptr<SDLWindow> MainClass::GetWindow() const {
        return window;
    }

    std::shared_ptr<TimeSystem> MainClass::GetTimeSystem() const {
        return time;
    }

    std::shared_ptr<AssetDatabase> MainClass::GetAssetDatabase() const {
        return asset_database;
    }

    std::shared_ptr<AssetManager> MainClass::GetAssetManager() const {
        return asset_manager;
    }

    std::shared_ptr<WorldSystem> MainClass::GetWorldSystem() const {
        return world;
    }

    std::shared_ptr<PhysicsSystem> MainClass::GetPhysicsSystem() const {
        return physics;
    }

    std::shared_ptr<GUISystem> MainClass::GetGUISystem() const {
        return gui;
    }

    std::shared_ptr<RenderSystem> MainClass::GetRenderSystem() const {
        return renderer;
    }

    std::shared_ptr<Input> MainClass::GetInputSystem() const {
        return input;
    }

    std::shared_ptr<ShaderCompiler> MainClass::GetShaderCompiler() const {
        return shader_compiler;
    }

    void MainClass::SetRenderGraph(
        std::unique_ptr<RenderGraph> render_graph, RGTextureHandle final_color_attachment_id
    ) {
        this->render_graph = std::move(render_graph);
        this->m_final_color_attachment_id = final_color_attachment_id;
    }

    void MainClass::RunOneFrame() {
        // TODO: asynchronous execution
        this->asset_manager->LoadAssetsInQueue();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                m_on_quit = true;
                break;
            }
            // this->gui->ProcessEvent(&event);
            // if (this->gui->WantCaptureMouse() && SDL_EVENT_MOUSE_MOTION <= event.type && event.type <
            // SDL_EVENT_JOYSTICK_AXIS_MOTION) // 0x600+
            //     continue;
            // if (this->gui->WantCaptureKeyboard() && (event.type == SDL_EVENT_KEY_DOWN || event.type ==
            // SDL_EVENT_KEY_UP))
            //     continue;
            if (this->input) input->ProcessEvent(&event);
        }

        if (this->input) this->input->Update(time->GetDeltaTime());
        this->world->GetMainSceneRef().FlushCmdQueue();
        // TODO: add input event
        this->world->GetMainSceneRef().AddTickEvent();
        // this->gui->PrepareGUI();

        this->world->GetMainSceneRef().ProcessEvents();
        this->world->GetMainSceneRef().FlushPhysics(*this->renderer);
        this->world->UpdateRendererData(*this->renderer);

        // Physics → render bridge: forward the physics model matrices buffer
        // to the scene data manager (physics itself no longer touches Render).
        if (auto *phys_scene = this->world->GetMainSceneRef().GetPhysicsScene()) {
            this->renderer->GetSceneDataManager().SetModelMatricesBuffer(phys_scene->GetGpuBuffers().model_matrices);
        }

        if (this->renderer->StartFrame() == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after the recreation retry (e.g. window
            // minimized or resized again mid-frame): skip this frame. The
            // frame state was left untouched by StartFrame, so the next frame
            // resumes cleanly.
            return;
        }
        // Phase 1: CPU-side physics prep (no CB needed).
        this->physics->PreGPUStep();
        // Phase 2: GPU recording — physics + rendering share one CB.
        auto cb = this->renderer->GetFrameManager().BeginMainCommandBuffer();
        this->physics->GPUStep(cb.GetCommandBuffer()); // physics solvers record their compute passes
        if (this->render_graph && this->render_graph->GetNumPasses() > 0) {
            this->render_graph->RecordAllPasses(cb.GetCommandBuffer());
        }

        // Phase 3: Frame completion — ends the main CB, records the copy CB,
        // submits one batch (main CB + copy CB) and presents.
        this->renderer->CompleteFrame(
            *this->render_graph->GetInternalTextureResource(this->m_final_color_attachment_id),
            Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite
        );

        // Phase 4: Physics readback / post-processing (batch already submitted).
        this->physics->PostGPUStep();
    }
} // namespace Engine
