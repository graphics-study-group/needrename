#include <SDL3/SDL.h>
#include <cassert>
#include <charconv>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Loader/Importer.h>
#include <Core/Delegate/FuncDelegate.h>
#include <Core/Functional/EventQueue.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/FullRenderSystem.h>
#include <SDL3/SDL.h>
#include <UserInterface/GUISystem.h>
#include <UserInterface/Input.h>
#include <cmake_config.h>

#include <Editor/Render/EditorRenderGraphBuilder.h>
#include <Editor/Widget/GameWidget.h>
#include <Editor/Widget/ProjectWidget.h>
#include <Editor/Widget/SceneWidget.h>
#include <Editor/Window/MainWindow.h>

#include "CustomComponent.h"
#include <meta_editor_run_game_example/reflection_init.ipp>

using namespace Engine;

namespace {
    struct ExampleOptions {
        int64_t max_frame_count = std::numeric_limits<int64_t>::max();
        bool keep_project = false;
    };

    ExampleOptions ParseArguments(int argc, char **argv) {
        ExampleOptions options{};
        bool frame_count_set = false;

        for (int arg_index = 1; arg_index < argc; ++arg_index) {
            std::string_view arg(argv[arg_index]);
            if (arg == "--keep-project") {
                options.keep_project = true;
                continue;
            }
            if (!frame_count_set) {
                int64_t parsed_value = 0;
                const auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), parsed_value);
                if (ec == std::errc{} && ptr == arg.data() + arg.size() && parsed_value > 0) {
                    options.max_frame_count = parsed_value;
                    frame_count_set = true;
                    continue;
                }
            }
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }

        return options;
    }

    void ResetExampleProject(
        const std::filesystem::path &project_template_path, const std::filesystem::path &project_path
    ) {
        std::error_code ec;
        std::filesystem::remove_all(project_path, ec);
        ec.clear();
        std::filesystem::copy(project_template_path, project_path, std::filesystem::copy_options::recursive);
    }

    void CleanupProject(const std::filesystem::path &project_path, bool keep_project) {
        if (keep_project) {
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "Keeping imported project directory for inspection: %s",
                project_path.string().c_str()
            );
            return;
        }

        std::error_code ec;
        std::filesystem::remove_all(project_path, ec);
        if (ec) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to clean temp project: %s", ec.message().c_str());
        }
    }

} // namespace

void Start() {
    auto cmc = MainClass::GetInstance();
    auto &scene = cmc->GetWorldSystem()->GetMainSceneRef();
    scene.ClearEventQueue();
    scene.AddInitEvent();
}

int main(int argc, char **argv) {
    const ExampleOptions options = ParseArguments(argc, argv);

    std::filesystem::path project_template_path(ENGINE_PROJECTS_DIR);
    project_template_path = project_template_path / "test_project";

    std::filesystem::path project_path(ENGINE_EXAMPLES_DIR);
    project_path = project_path / "editor_run_game_example" / "temp_project";

    SDL_Init(SDL_INIT_VIDEO);

    int displayIndex = 1;
    auto displayMode = SDL_GetDesktopDisplayMode(displayIndex);
    if (displayMode == nullptr) {
        SDL_Log("Failed to get display mode: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    int screenWidth = displayMode->w;
    int screenHeight = displayMode->h;
    SDL_Log("Screen Resolution: %dx%d @ %fHz", screenWidth, screenHeight, displayMode->refresh_rate);
    StartupOptions opt{.resol_x = (int)(screenWidth * 0.9), .resol_y = (int)(screenHeight * 0.9), .title = "Editor"};

    ResetExampleProject(project_template_path, project_path);

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    RegisterAllTypes();
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    auto rsys = cmc->GetRenderSystem();
    auto asys = cmc->GetAssetManager();
    auto world = cmc->GetWorldSystem();
    auto gui = cmc->GetGUISystem();
    gui->CreateVulkanBackend(*rsys, ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Add extra objects");
    auto input = MainClass::GetInstance()->GetInputSystem();
    input->AddAxis(Input::ButtonAxis("move forward", Input::AxisType::TypeKey, "w", "s"));
    input->AddAxis(Input::ButtonAxis("move right", Input::AxisType::TypeKey, "d", "a"));
    input->AddAxis(Input::ButtonAxis("move up", Input::AxisType::TypeKey, "space", "left shift"));
    input->AddAxis(Input::ButtonAxis("roll right", Input::AxisType::TypeKey, "e", "q"));
    input->AddAxis(
        Input::MotionAxis("look x", Input::AxisType::TypeMouseMotion, "x", 0.3f, 3.0f, 0.001f, 3.0f, false, true)
    );
    input->AddAxis(
        Input::MotionAxis("look y", Input::AxisType::TypeMouseMotion, "y", 0.3f, 3.0f, 0.001f, 3.0f, false, true)
    );

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Create Editor Window");
    Editor::MainWindow main_window;
    auto scene_widget = std::make_shared<Editor::SceneWidget>(Editor::MainWindow::k_scene_widget_name);
    main_window.AddWidget(scene_widget);
    auto game_widget = std::make_shared<Editor::GameWidget>(Editor::MainWindow::k_game_widget_name);
    main_window.AddWidget(game_widget);
    auto project_widget = main_window.FindWidgetAs<Editor::ProjectWidget>(Editor::MainWindow::k_project_widget_name);

    auto rgb = std::make_unique<Editor::EditorRenderGraphBuilder>(*cmc->GetRenderSystem());
    int32_t final_color_id, scene_color_id, game_color_id;
    auto rg = rgb->BuildEditorRenderGraph(
        screenWidth, screenHeight, scene_widget.get(), game_widget.get(), scene_color_id, game_color_id, final_color_id
    );

    auto scene_texture = rg->GetInternalTextureResource(scene_color_id);
    scene_widget->SetDisplayTexture(*scene_texture);
    auto game_texture = rg->GetInternalTextureResource(game_color_id);
    game_widget->SetDisplayTexture(*game_texture);

    main_window.m_OnStart.AddDelegate(std::make_unique<FuncDelegate<>>(Start));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");

    bool onQuit = false;
    int64_t frame_count = 0;
    while (!onQuit && frame_count < options.max_frame_count) {
        ++frame_count;
        cmc->GetTimeSystem()->NextFrame();

        asys->LoadAssetsInQueue();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                onQuit = true;
                break;
            }

            if (event.type == SDL_EVENT_DROP_FILE) {
                const char *dropped = event.drop.data;
                if (dropped && project_widget) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "File dropped: %s", event.drop.data);
                    try {
                        const std::filesystem::path source_path(dropped);
                        const std::filesystem::path target_dir(project_widget->GetCurrentPath().generic_string());
                        Importer::ImportExternalResource(source_path, target_dir);
                        project_widget->RefreshCurrentDirectory();
                        SDL_LogInfo(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Imported dropped file %s into %s",
                            source_path.string().c_str(),
                            target_dir.string().c_str()
                        );
                    } catch (const std::exception &e) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to import dropped file: %s", e.what());
                    }
                }
            }

            gui->ProcessEvent(&event);
            if (game_widget->m_accept_input) cmc->GetInputSystem()->ProcessEvent(&event);
        }

        if (game_widget->m_accept_input) cmc->GetInputSystem()->Update();
        else cmc->GetInputSystem()->ResetAxes();
        world->GetMainSceneRef().FlushCmdQueue();

        if (main_window.m_is_playing) {
            world->GetMainSceneRef().AddTickEvent();
            world->GetMainSceneRef().ProcessEvents();
        }
        world->UpdateRendererData(*rsys);

        gui->PrepareGUI();
        main_window.Render();

        rsys->StartFrame();
        rg->Execute();
        auto [w, h] = cmc->GetWindow()->GetSize();
        rsys->CompleteFrame(*rg->GetInternalTextureResource(final_color_id), w, h);
    }
    rsys->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    CleanupProject(project_path, options.keep_project);
    return 0;
}
