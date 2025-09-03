#include "MainClass.h"

#include "Asset/AssetManager/AssetManager.h"
#include "Framework/world/WorldSystem.h"
#include "UserInterface/GUISystem.h"
#include <Asset/Scene/LevelAsset.h>
#include <Core/Functional/EventQueue.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <UserInterface/Input.h>
#include <Render/FullRenderSystem.h>

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

        std::shared_ptr <MainClass> sptr{nullptr};
        std::call_once(MainClass::m_instance_ready, [&] {
            sptr = std::make_shared<MainClass>();
            MainClass::m_instance = sptr;
        });
        return sptr;
    }

    MainClass::~MainClass() {
        SDL_Quit();
    }

    void MainClass::LoadProject(const std::filesystem::path &path) {
        this->asset->LoadProject(path);

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
        auto level_asset = std::dynamic_pointer_cast<LevelAsset>(this->asset->LoadAssetImmediately(default_level_guid));
        this->world->LoadLevelAsset(level_asset);
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
        this->asset = std::make_shared<AssetManager>();
        this->gui = std::make_shared<GUISystem>(this->renderer);
        this->input = std::make_shared<Input>();
        this->event_queue = std::make_shared<EventQueue>();

        this->renderer->Create();
        this->window->CreateRenderTargets(this->renderer);
        this->gui->Create(this->window->GetWindow());
        Reflection::Initialize();
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

    std::shared_ptr<AssetManager> MainClass::GetAssetManager() const {
        return asset;
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

    void MainClass::RunOneFrame() {
        // TODO: asynchronous execution
        this->asset->LoadAssetsInQueue();

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
        this->world->LoadGameObjectInQueue();
        // TODO: add input event
        this->world->AddTickEvent();
        // this->gui->PrepareGUI();

        this->event_queue->ProcessEvents();

        this->renderer->StartFrame();
        auto context = this->renderer->GetFrameManager().GetGraphicsContext();
        GraphicsCommandBuffer &cb = dynamic_cast<GraphicsCommandBuffer &>(context.GetCommandBuffer());

        cb.Begin();
        context.UseImage(
            this->window->GetColorTexture(),
            GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.UseImage(
            this->window->GetDepthTexture(),
            GraphicsContext::ImageGraphicsAccessType::DepthAttachmentWrite,
            GraphicsContext::ImageAccessType::None
        );
        context.PrepareCommandBuffer();
        cb.BeginRendering(
            {&this->window->GetColorTexture(),
             nullptr,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::Store},
            {&this->window->GetDepthTexture(),
             nullptr,
             AttachmentUtils::LoadOperation::Clear,
             AttachmentUtils::StoreOperation::DontCare,
             AttachmentUtils::DepthClearValue{1.0f, 0U}},
            this->window->GetExtent(),
            "Main Pass"
        );
        this->renderer->SetActiveCamera(this->world->m_active_camera);
        cb.DrawRenderers(renderer->GetRendererManager().FilterAndSortRenderers({}), 0);
        cb.EndRendering();

        // context.UseImage(this->window->GetColorTexture(),
        // GraphicsContext::ImageGraphicsAccessType::ColorAttachmentWrite,
        // GraphicsContext::ImageAccessType::ColorAttachmentWrite); context.PrepareCommandBuffer();
        // this->gui->DrawGUI({this->window->GetColorTexture().GetImage(),
        //                     this->window->GetColorTexture().GetImageView(),
        //                     vk::AttachmentLoadOp::eLoad,
        //                     vk::AttachmentStoreOp::eStore},
        //                    this->window->GetExtent(), cb);

        cb.End();
        this->renderer->GetFrameManager().SubmitMainCommandBuffer();
        this->renderer->GetFrameManager().StageBlitComposition(
            this->window->GetColorTexture().GetImage(), this->window->GetExtent(), this->window->GetExtent()
        );
        this->renderer->CompleteFrame();
    }
} // namespace Engine
