#include <Core/FileSystem/DataRoot.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Core/Delegate/FuncDelegate.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Functional/Time.h>
#include <Framework/Bridge/PhysicsAdaptor.h>
#include <Framework/Component/RenderComponent/CameraComponent.h>
#include <Framework/Import/Importer.h>
#include <Framework/Input/Input.h>
#include <Framework/MainClass.h>
#include <Framework/Object/GameObject.h>
#include <Framework/World/EventQueue.h>
#include <Framework/World/WorldSystem.h>
#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSystem.h>
#include <Render/FullRenderSystem.h>
#include <Render/RenderSystem/IPresentProvider.h>
#include <Render/UserInterface/GUISystem.h>
#include <SDL3/SDL.h>

#include <Editor/EditorMainClass.h>
#include <Editor/Render/EditorRenderGraphBuilder.h>
#include <Editor/Widget/GameWidget.h>
#include <Editor/Widget/ProjectWidget.h>
#include <Editor/Widget/SceneWidget.h>
#include <Editor/Window/MainWindow.h>

#include "CustomComponent.h"
#include "meta_editor_run_game_example/reflection_init.inc"

using namespace Engine;

#if defined(__EMSCRIPTEN__)
namespace {
    /// The engine resolves its data root by looking next to the executable. A page has no such
    /// place, so the tree is preloaded at `/` and the root is pointed there.
    struct DataRootForBrowser {
        DataRootForBrowser() {
            setenv("NEEDRENAME_DATA_ROOT", "/", 0);
        }
    };
    const DataRootForBrowser g_data_root_for_browser{};

    /// A frame bound from the page URL, used by a headless capture: `?frames=N`.
    int64_t BrowserFrameLimit() {
        const char *search = emscripten_run_script_string("window.location.search");
        const char *marker = search != nullptr ? std::strstr(search, "frames=") : nullptr;
        return marker != nullptr ? std::atoll(marker + 7) : 0;
    }
} // namespace
#endif

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
    scene.GetPhysicsAdaptor().SetPhysicsActive(true);
    if (auto *ps = scene.GetPhysicsScene()) {
        ps->SetSimulationEnabled(true);
    }
}

void Stop() {
    auto cmc = MainClass::GetInstance();
    auto &scene = cmc->GetWorldSystem()->GetMainSceneRef();
    scene.GetPhysicsAdaptor().SetPhysicsActive(false);
    if (auto *ps = scene.GetPhysicsScene()) {
        ps->SetSimulationEnabled(false);
    }
}

int RunExample(int argc, char **argv);

int main(int argc, char **argv) {
    try {
        return RunExample(argc, argv);
    } catch (const std::exception &error) {
        // Installed executables must fail with a diagnostic, not a crash: the
        // data locator names the missing data directory and the resolution step.
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
        return 1;
    }
}

int RunExample(int argc, char **argv) {
    const ExampleOptions options = ParseArguments(argc, argv);

    std::filesystem::path project_template_path(ProjectsDir());
    project_template_path = project_template_path / "test_project";

    // Runtime scratch: the working copy of the project is materialised in the
    // platform's temporary directory. It must not land in the working directory,
    // because that may be the product directory, which has to stay exactly what
    // the build assembled. `--keep-project` logs the path for inspection.
    const std::filesystem::path project_path =
        std::filesystem::temp_directory_path() / "needrename_editor_project";

    SDL_Init(SDL_INIT_VIDEO);

    int screenWidth = 1280;
    int screenHeight = 720;
#if defined(__EMSCRIPTEN__)
    // A page has exactly one display, and its reported size is the browser window's; the engine
    // sizes the canvas from these numbers, so they are the page's viewport rather than a mode.
#else
    // Prefer the second monitor when there is one (the editor was written for a dual-head setup),
    // and fall back to the primary when there is not.
    auto displayMode = SDL_GetDesktopDisplayMode(1);
    if (displayMode == nullptr) {
        displayMode = SDL_GetDesktopDisplayMode(0);
    }
    if (displayMode != nullptr) {
        screenWidth = displayMode->w;
        screenHeight = displayMode->h;
    }
    SDL_Log("Screen Resolution: %dx%d", screenWidth, screenHeight);
#endif
    StartupOptions opt{.resol_x = (int)(screenWidth * 0.9), .resol_y = (int)(screenHeight * 0.9), .title = "Editor"};

    ResetExampleProject(project_template_path, project_path);

    auto emc = Editor::EditorMainClass::GetInstance();
    auto cmc = MainClass::GetInstance();

    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    RegisterAllTypes();
    cmc->LoadBuiltinAssets(BuiltinAssetsDir());
    auto rsys = cmc->GetRenderSystem();
    auto asys = cmc->GetAssetManager();
    auto world = cmc->GetWorldSystem();
    auto gui = cmc->GetGUISystem();
    gui->CreateBackend(*rsys, Engine::Rhi::ImageFormat::R8G8B8A8UNorm);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);
    emc->Initialize();

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
    RGTextureHandle final_color_id, scene_color_id, game_color_id;
    // Build the graph at the size the window actually presents at, not at the display's size: the
    // final target is copied into the surface, and a mismatch would present a sub-rectangle.
    const Rhi::Extent2D present_extent = rsys->GetPresentProvider().GetExtent();
    auto rg = rgb->BuildEditorRenderGraph(
        present_extent.width,
        present_extent.height,
        scene_widget.get(),
        game_widget.get(),
        scene_color_id,
        game_color_id,
        final_color_id
    );

    auto scene_texture = rg->GetInternalTextureResource(scene_color_id);
    auto game_texture = rg->GetInternalTextureResource(game_color_id);
    SDL_LogInfo(
        SDL_LOG_CATEGORY_APPLICATION,
        "[main] Init: scene_handle=%d game_handle=%d scene_tex=%p game_tex=%p",
        static_cast<int32_t>(scene_color_id),
        static_cast<int32_t>(game_color_id),
        static_cast<const void *>(scene_texture),
        static_cast<const void *>(game_texture)
    );
    scene_widget->SetDisplayTexture(*scene_texture);
    game_widget->SetDisplayTexture(*game_texture);

    main_window.m_OnStart.AddDelegate(std::make_unique<FuncDelegate<>>(Start));
    main_window.m_OnStop.AddDelegate(std::make_unique<FuncDelegate<>>(Stop));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");

    bool onQuit = false;
    int64_t frame_count = 0;
    int64_t max_frames = options.max_frame_count;
#if defined(__EMSCRIPTEN__)
    // A page has no command line the shell can set from a URL, and a headless capture needs a
    // finite run; `?frames=N` is that bound. Without it the editor runs until the page is closed.
    if (max_frames == std::numeric_limits<int64_t>::max()) {
        const int64_t browser_limit = BrowserFrameLimit();
        if (browser_limit > 0) {
            max_frames = browser_limit;
        }
    }
#endif
    while (!onQuit && frame_count < max_frames) {
        ++frame_count;
#if defined(__EMSCRIPTEN__)
        // This loop is what turns the page: the frame's waits poll the WebGPU instance, and a
        // browser only delivers those callbacks once control returns to it.
        emscripten_sleep(1);
#endif
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
                        const std::filesystem::path target_dir = project_widget->GetCurrentPath().GetSubPath();
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

        if (game_widget->m_accept_input) cmc->GetInputSystem()->Update(cmc->GetTimeSystem()->GetDeltaTime());
        else cmc->GetInputSystem()->ResetAxes();
        world->GetMainSceneRef().FlushCmdQueue();

        if (main_window.m_is_playing) {
            world->GetMainSceneRef().AddTickEvent();
            world->GetMainSceneRef().ProcessEvents();
        }
        world->GetMainSceneRef().FlushPhysics(*rsys);

        world->UpdateRendererData(*rsys);
        // Size the render-owned model matrices buffer for this frame's rigid
        // body slot count before StartFrame writes the frame's descriptor set,
        // so the handle bound for the frame is final.
        if (auto *phys_scene = world->GetMainSceneRef().GetPhysicsScene()) {
            rsys->GetSceneDataManager().EnsureModelMatricesCapacity(phys_scene->GetGpuBuffers().rigid_body_slot_count);
        }

        gui->PrepareGUI();
        main_window.Render();

        if (rsys->StartFrame() == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after the recreation retry: skip this
            // frame (frame state untouched, next frame resumes cleanly).
            continue;
        }

        auto cb = rsys->GetFrameManager().BeginMainCommandBuffer();

        if (main_window.m_is_playing) {
            cmc->GetPhysicsSystem()->GPUStep(cb.GetEncoder());
        }
        // Model matrix production is NOT gated on the play state: the physics
        // driven renderers read their transforms from this buffer in both
        // states, so every rendered frame produces them, after the step and
        // before the render graph's passes.
        if (auto *phys_scene = world->GetMainSceneRef().GetPhysicsScene()) {
            cmc->GetPhysicsSystem()->GPUCalcModelMatrices(
                *phys_scene, cb.GetEncoder(), rsys->GetSceneDataManager().GetModelMatricesBuffer()
            );
        }
        rg->RecordAllPasses(cb.GetEncoder());

        rsys->CompleteFrame(
            *rg->GetInternalTextureResource(final_color_id), Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite
        );
    }
    rsys->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    CleanupProject(project_path, options.keep_project);
    return 0;
}
