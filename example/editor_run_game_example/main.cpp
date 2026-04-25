#include <SDL3/SDL.h>
#include <cassert>
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/Loader/Importer.h>
#include <Asset/Scene/SceneAsset.h>
#include <Core/Delegate/FuncDelegate.h>
#include <Core/Functional/EventQueue.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <Framework/component/Component.h>
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
#include <Editor/Widget/SceneWidget.h>
#include <Editor/Window/MainWindow.h>

#include "CustomComponent.h"
#include <meta_editor_run_game_example/reflection_init.ipp>

using namespace Engine;

namespace {
    struct ExampleOptions {
        int64_t max_frame_count = std::numeric_limits<int64_t>::max();
        bool keep_project = false;
        std::filesystem::path import_source = std::filesystem::path(ENGINE_ASSETS_DIR) / "pp-19-01_vityaz" / "pp-19-01_vityaz.fbx";
    };

    std::filesystem::path ResolveImportPath(const std::filesystem::path &input_path) {
        if (input_path.is_absolute()) {
            return input_path;
        }

        std::error_code ec;
        auto from_cwd = std::filesystem::weakly_canonical(std::filesystem::current_path() / input_path, ec);
        if (!ec && std::filesystem::exists(from_cwd)) {
            return from_cwd;
        }

        ec.clear();
        return std::filesystem::weakly_canonical(std::filesystem::path(ENGINE_ROOT_DIR) / input_path, ec);
    }

    ExampleOptions ParseArguments(int argc, char **argv) {
        ExampleOptions options{};
        bool frame_count_set = false;
        bool import_source_set = false;

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
            if (!import_source_set) {
                options.import_source = ResolveImportPath(std::filesystem::path(arg));
                import_source_set = true;
                continue;
            }
            throw std::runtime_error("Too many positional arguments.");
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

    void ImportModelPrefab(
        const ExampleOptions &options,
        FileSystemDatabase &database,
        Scene &main_scene,
        const std::filesystem::path &path_in_project
    ) {
        if (!path_in_project.empty()) {
            std::filesystem::create_directories(database.GetProjectAssetsPath() / path_in_project);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Import source: %s", options.import_source.string().c_str());
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION, "Import extension: %s", options.import_source.extension().string().c_str()
        );
        Importer::ImportExternalResource(options.import_source, path_in_project);

        const std::string prefab_name = "GO_" + options.import_source.stem().string() + ".asset";
        AssetPath prefab_path{database, path_in_project / prefab_name};
        auto prefab_ref = database.GetNewAssetRef(prefab_path);
        prefab_ref.as<SceneAsset>()->AddToScene(main_scene);
        main_scene.FlushCmdQueue();
    }
} // namespace

SpinningComponent::SpinningComponent(const GameObject &parent) : Component(parent) {
}

void SpinningComponent::Init() {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SpinningComponent Init");
}

void SpinningComponent::Tick() {
    float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
    auto go = GetParentGameObject();
    if (go) {
        auto &transform = go->GetTransformRef();
        transform.SetRotation(
            transform.GetRotation() * glm::angleAxis(glm::radians(m_speed * dt), glm::vec3(0.0f, 0.0f, 1.0f))
        );
    }
}

ControlComponent::ControlComponent(const GameObject &parent) : Component(parent) {
}

void ControlComponent::Tick() {
    auto input = MainClass::GetInstance()->GetInputSystem();
    auto move_forward = input->GetAxis("move forward");
    auto move_backward = input->GetAxis("move backward");
    auto move_right = input->GetAxis("move right");
    auto move_up = input->GetAxis("move up");
    auto roll_right = input->GetAxisRaw("roll right");
    auto look_x = input->GetAxisRaw("look x");
    auto look_y = input->GetAxisRaw("look y");
    Transform &transform = GetParentGameObject()->GetTransformRef();
    float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
    transform.SetRotation(
        transform.GetRotation()
        * glm::quat(
            glm::vec3{look_y * m_rotation_speed * dt, roll_right * m_roll_speed * dt, look_x * m_rotation_speed * dt}
        )
    );
    transform.SetPosition(
        transform.GetPosition()
        + transform.GetRotation() * glm::vec3{move_right, move_forward + move_backward, move_up} * m_move_speed * dt
    );
}

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
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    auto world = cmc->GetWorldSystem();
    auto gui = cmc->GetGUISystem();
    auto &main_scene = world->GetMainSceneRef();
    gui->CreateVulkanBackend(*rsys, ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);

    ImportModelPrefab(options, *adb, main_scene, std::filesystem::path("imported_preview"));

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
