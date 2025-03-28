#include "MainClass.h"

#include "Framework/world/WorldSystem.h"
#include "Render/RenderSystem.h"
#include "Asset/AssetManager/AssetManager.h"
#include "GUI/GUISystem.h"
#include <Input/DefaultInput.h>
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

        this->renderer = std::make_shared<RenderSystem>(this->window);
        this->world = std::make_shared<WorldSystem>();
        this->asset = std::make_shared<AssetManager>();
        this->gui = std::make_shared<GUISystem>(this->renderer);
        this->input = std::make_shared<Input>();

        this->renderer->Create();
        this->gui->Create(this->window->GetWindow());
        Reflection::Initialize();
    }

    void MainClass::MainLoop()
    {
        unsigned int FPS_TIMER = 0;
        while (!m_on_quit)
        {
            float current_time = SDL_GetTicks();
            float dt = (current_time - FPS_TIMER) / 1000.0f;
            FPS_TIMER = current_time;

            this->RunOneFrame(dt);
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
        renderer->WaitForIdle();
        renderer->ClearComponent();
    }

    void MainClass::LoopFiniteFrame(int max_frame_count)
    {
        unsigned int FPS_TIMER = 0;
        int frame_count = 0;

        while (!m_on_quit && frame_count < max_frame_count)
        {
            float current_time = SDL_GetTicks();
            float dt = (current_time - FPS_TIMER) / 1000.0f;
            FPS_TIMER = current_time;

            this->RunOneFrame(dt);
            frame_count++;
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
        renderer->WaitForIdle();
        renderer->ClearComponent();
    }

    void MainClass::LoopFiniteTime(float max_time)
    {
        unsigned int FPS_TIMER = 0;
        float current_time = SDL_GetTicks();
        float start_time = current_time;

        while (!m_on_quit && current_time - start_time < max_time)
        {
            float current_time = SDL_GetTicks();
            float dt = (current_time - FPS_TIMER) / 1000.0f;
            FPS_TIMER = current_time;

            this->RunOneFrame(dt);
        }
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
        renderer->WaitForIdle();
        renderer->ClearComponent();
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

    void MainClass::RunOneFrame(float dt)
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
            this->gui->ProcessEvent(&event);
            if (this->gui->WantCaptureMouse() && SDL_EVENT_MOUSE_MOTION <= event.type && event.type < SDL_EVENT_JOYSTICK_AXIS_MOTION) // 0x600+
                continue;
            if (this->gui->WantCaptureKeyboard() && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
                continue;
            for (auto &input : this->inputs)
            {
                input->ProcessEvent(&event);
            }
        }

        this->world->Tick(dt);

        // TODO: Set up viewport information

        this->renderer->Render();
    }
} // namespace Engine
