#include "MainClass.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Scene/LevelAsset.h>
#include <Asset/Shader/ShaderCompiler.h>
#include <Asset/Material/MaterialAsset.h>
#include <Core/Functional/EventQueue.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <Framework/world/WorldSystem.h>
#include <Render/FullRenderSystem.h>
#include <UserInterface/GUISystem.h>
#include <UserInterface/Input.h>
#include <Render/Pipeline/RenderGraph/ComplexRenderGraphBuilder.h>

#include <glslang/Public/ShaderLang.h>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>

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
        auto level_asset =
            std::dynamic_pointer_cast<LevelAsset>(this->asset_manager->LoadAssetImmediately(default_level_guid));
        
        if (level_asset->m_skybox_material) {
            this->asset_manager->LoadAssetImmediately(level_asset->m_skybox_material);
            this->asset_manager->LoadAssetsInQueue();
            // XXX: like RendererComponent.cpp, this is a temporary solution. simply check the m_name
            auto lib = level_asset->m_skybox_material->as<MaterialAsset>()->m_library;
            this->renderer->GetMaterialRegistry().AddMaterial(lib);
            auto material_instance = std::make_shared<MaterialInstance>(
                *(this->renderer),
                *this->renderer->GetMaterialRegistry().GetMaterial(lib->cas<MaterialLibraryAsset>()->m_name)
            );
            material_instance->Instantiate(*level_asset->m_skybox_material->as<MaterialAsset>());
            this->renderer->GetSceneDataManager().SetSkyboxMaterial(material_instance);
        }

        auto active_camera = this->world->GetActiveCamera();
        if (active_camera)
            this->renderer->GetCameraManager().RegisterCamera(active_camera);

        // XXX: this should load from pipeline asset. it contains shader asset (comp)
        this->render_graph_builder = std::make_unique<ComplexRenderGraphBuilder>(*this->renderer);
        this->render_graph = std::move(this->render_graph_builder->BuildDefaultRenderGraph(this->window->GetColorTexture(), this->window->GetDepthTexture()));
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
        this->window = std::make_shared<SDLWindow>(opt->title.c_str(), opt->resol_x, opt->resol_y, sdl_window_flags);
        this->time = std::make_shared<TimeSystem>();
        this->renderer = std::make_shared<RenderSystem>(this->window);
        this->world = std::make_shared<WorldSystem>();
        this->asset_database = std::make_shared<FileSystemDatabase>();
        this->asset_manager = std::make_shared<AssetManager>();
        this->gui = std::make_shared<GUISystem>();
        this->input = std::make_shared<Input>();
        this->event_queue = std::make_shared<EventQueue>(*this->world);

        this->renderer->Create();
        this->window->CreateRenderTargets(this->renderer);
        this->gui->Create(this->window->GetWindow());
        Reflection::Initialize();

        // if in editor mode
        this->shader_compiler = std::make_shared<ShaderCompiler>();
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

    std::shared_ptr<GUISystem> MainClass::GetGUISystem() const {
        return gui;
    }

    std::shared_ptr<RenderSystem> MainClass::GetRenderSystem() const {
        return renderer;
    }

    std::shared_ptr<Input> MainClass::GetInputSystem() const {
        return input;
    }

    std::shared_ptr<EventQueue> MainClass::GetEventQueue() const {
        return event_queue;
    }

    std::shared_ptr<ShaderCompiler> MainClass::GetShaderCompiler() {
        return shader_compiler;
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
            input->ProcessEvent(&event);
        }

        this->input->Update();
        this->world->FlushCmdQueue();
        // TODO: add input event
        this->world->AddTickEvent();
        // this->gui->PrepareGUI();

        this->event_queue->ProcessEvents();

        this->world->UpdateLightData(this->renderer->GetSceneDataManager());

        this->renderer->StartFrame();
        this->render_graph->Execute();
        this->renderer->CompleteFrame(
            *this->window->GetColorTexture(),
            this->render_graph_builder->GetColorAttachmentAccessType(),
            this->window->GetExtent().width,
            this->window->GetExtent().height
        );
    }
} // namespace Engine
