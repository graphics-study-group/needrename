#include "MainClass.h"

#include "Framework/world/WorldSystem.h"
#include "Render/RenderSystem.h"
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/Memory/Buffer.h>
#include "Asset/AssetManager/AssetManager.h"
#include "GUI/GUISystem.h"
#include <Input/Input.h>
#include <Functional/SDLWindow.h>
#include <Functional/Time.h>
#include <Asset/Scene/LevelAsset.h>

#include <nlohmann/json.hpp>
#include <fstream>
#include <exception>
#include <mutex>

namespace Engine
{
    static std::shared_ptr<MainClass> g_main_class_ptr = nullptr;
    static std::once_flag g_main_class_flag;

    std::shared_ptr<MainClass> MainClass::GetInstance()
    {
        std::call_once(g_main_class_flag, [&]
                       { g_main_class_ptr = std::shared_ptr<MainClass>(new MainClass()); });
        return g_main_class_ptr;
    }

    MainClass::~MainClass()
    {
        SDL_Quit();
    }

    void MainClass::LoadProject(const std::filesystem::path &path)
    {
        this->asset->LoadProject(path);

        nlohmann::json project_config;
        std::ifstream file(path / "project.config");
        if (file.is_open())
        {
            project_config = nlohmann::json::parse(file);
            file.close();
        }
        else
        {
            throw std::runtime_error("Cannot open project.config");
        }
        assert(project_config.contains("default_level"));
        GUID default_level_guid(project_config["default_level"].get<std::string>());
        auto level_asset = std::dynamic_pointer_cast<LevelAsset>(this->asset->LoadAssetImmediately(default_level_guid));
        this->world->LoadLevelAsset(level_asset);
    }

    void MainClass::Initialize(const StartupOptions *opt, Uint32 sdl_init_flags, SDL_LogPriority sdl_logPrior, Uint32 sdl_window_flags)
    {
        if (!SDL_Init(sdl_init_flags))
            throw Exception::SDLExceptions::cant_init();
        SDL_SetLogPriorities(sdl_logPrior);
        this->window = nullptr;

        if (sdl_window_flags == 0)
            sdl_window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (opt->instantQuit)
            return;
        this->window = std::make_shared<SDLWindow>(opt->title.c_str(), opt->resol_x, opt->resol_y, sdl_window_flags);
        this->time = std::make_shared<TimeSystem>();
        this->renderer = std::make_shared<RenderSystem>(this->window);
        this->world = std::make_shared<WorldSystem>();
        this->asset = std::make_shared<AssetManager>();
        this->gui = std::make_shared<GUISystem>(this->renderer);
        this->input = std::make_shared<Input>();

        this->renderer->Create();
        this->window->CreateRenderTargetBinding(this->renderer);
        this->gui->Create(this->window->GetWindow());
        Reflection::Initialize();
    }

    void MainClass::MainLoop()
    {
        while (!m_on_quit)
        {
            this->time->NextFrame();
            this->RunOneFrame();
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
        renderer->WaitForIdle();
        renderer->ClearComponent();
    }

    void MainClass::LoopFinite(uint64_t max_frame_count, float max_time_seconds)
    {
        while (!m_on_quit)
        {
            this->time->NextFrame();
            this->RunOneFrame();
            if (max_frame_count > 0 && this->time->GetFrameCount() >= max_frame_count)
                break;
            if (max_time_seconds > 0.0f && this->time->GetDeltaTimeInSeconds() >= max_time_seconds)
                break;
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
        renderer->WaitForIdle();
        renderer->ClearComponent();
    }

    std::shared_ptr<SDLWindow> MainClass::GetWindow() const
    {
        return window;
    }

    std::shared_ptr<TimeSystem> MainClass::GetTimeSystem() const
    {
        return time;
    }

    std::shared_ptr<AssetManager> MainClass::GetAssetManager() const
    {
        return asset;
    }

    std::shared_ptr<WorldSystem> MainClass::GetWorldSystem() const
    {
        return world;
    }

    std::shared_ptr<GUISystem> MainClass::GetGUISystem() const
    {
        return gui;
    }

    std::shared_ptr<RenderSystem> MainClass::GetRenderSystem() const
    {
        return renderer;
    }

    std::shared_ptr<Input> MainClass::GetInputSystem() const
    {
        return input;
    }

    void MainClass::RunOneFrame()
    {
        // TODO: asynchronous execution
        this->asset->LoadAssetsInQueue();

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                m_on_quit = true;
                break;
            }
            // TODO: add gui system in engine after the events between editor and the game are separated
            // this->gui->ProcessEvent(&event);
            // if (this->gui->WantCaptureMouse() && SDL_EVENT_MOUSE_MOTION <= event.type && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) // 0x600+
            //     continue;
            // if (this->gui->WantCaptureKeyboard() && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
            //     continue;
            input->ProcessEvent(&event);
        }

        this->input->Update();
        this->world->LoadGameObjectInQueue();
        this->world->Tick();

        // TODO: Set up viewport information
        renderer->StartFrame();
        RenderCommandBuffer &cb = renderer->GetCurrentCommandBuffer();
        cb.Begin();
        cb.BeginRendering(window->GetRenderTargetBinding(), window->GetExtent());
        renderer->DrawMeshes();
        cb.EndRendering();
        cb.End();
        cb.Submit(this->time->GetFrameCount() != 1);
        renderer->GetFrameManager().StageCopyComposition(window->GetRenderTargetBinding().GetColorAttachments()[0].image);
        renderer->CompleteFrame();
    }
} // namespace Engine
