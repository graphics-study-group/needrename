#include <Core/FileSystem/DataRoot.h>
#include <SDL3/SDL.h>
#if defined(__EMSCRIPTEN__)
#include <cstdlib>
#endif
#include <cassert>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Core/Functional/SDLWindow.h>
#include <Core/Math/Transform.h>
#include <Framework/Component/RenderComponent/CameraComponent.h>
#include <Framework/Import/Importer.h>
#include <Framework/MainClass.h>
#include <Framework/Object/GameObject.h>
#include <Framework/Scene/LevelAsset.h>
#include <Framework/Scene/SceneAsset.h>
#include <Framework/Tools/ComplexRenderGraphBuilder.h>
#include <Framework/World/Scene.h>
#include <Framework/World/WorldSystem.h>
#include <Render/FullRenderSystem.h>

using namespace Engine;

#if defined(__EMSCRIPTEN__)
namespace {
    /// Point the data root at where the preloaded tree lands. In a browser there is no directory
    /// next to the executable to look in, so the resolution cannot succeed by itself.
    struct DataRootForBrowser {
        DataRootForBrowser() {
            setenv("NEEDRENAME_DATA_ROOT", "/", 0);
        }
    };
    const DataRootForBrowser g_data_root_for_browser{};
} // namespace
#endif

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
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }
    // A diagnostic run may leave part of the frame out, which is how a fault that only one
    // runtime shows is narrowed down. The browser states these through `Module.arguments`,
    // because it has no environment to set before the page starts.
    uint32_t diagnostic_skip = 0;
    for (int index = 1; index < argc; index++) {
        const std::string_view argument{argv[index]};
        if (argument == "--skip-physics") {
            diagnostic_skip |= static_cast<uint32_t>(MainClass::DiagnosticSkip::Physics);
        } else if (argument == "--skip-matrices") {
            diagnostic_skip |= static_cast<uint32_t>(MainClass::DiagnosticSkip::ModelMatrices);
        } else if (argument == "--skip-passes") {
            diagnostic_skip |= static_cast<uint32_t>(MainClass::DiagnosticSkip::Passes);
        } else if (argument == "--skip-shadow-passes") {
            diagnostic_skip |= static_cast<uint32_t>(MainClass::DiagnosticSkip::ShadowPasses);
        } else if (argument == "--skip-compute-passes") {
            diagnostic_skip |= static_cast<uint32_t>(MainClass::DiagnosticSkip::ComputePasses);
        } else if (argument == "--skip-shadow-draws") {
            diagnostic_skip |= static_cast<uint32_t>(MainClass::DiagnosticSkip::ShadowDraws);
        } else if (argument == "--skip-lit-pass") {
            diagnostic_skip |= static_cast<uint32_t>(MainClass::DiagnosticSkip::LitPass);
        }
    }
    if (diagnostic_skip != 0) {
        MainClass::SetDiagnosticSkip(static_cast<MainClass::DiagnosticSkip>(diagnostic_skip));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "diagnostic skips: 0x%x", diagnostic_skip);
    }

    // Runtime scratch: materialise the working project in the platform's temporary
    // directory, never in the working directory (which may be the product
    // directory, and must stay exactly what the build assembled) and never in the
    // read-only data root.
    const std::filesystem::path project_path =
        std::filesystem::temp_directory_path() / "needrename_external_resource_project";
    if (std::filesystem::exists(project_path)) std::filesystem::remove_all(project_path);
    std::filesystem::copy(ProjectsDir() / "test_project", project_path, std::filesystem::copy_options::recursive);

    std::filesystem::path mesh_path(AssetsDir());
    // mesh_path = mesh_path / "meshes" / "sphere.obj";
    // mesh_path = mesh_path / "meshes" / "cube.obj";
    // mesh_path = mesh_path / "red_brick_sphere" / "red_brick_sphere.obj";
    mesh_path = mesh_path / "red_brick_sphere" / "red_brick_sphere_512.glb";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "External Resource Loading Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    auto asys = cmc->GetAssetManager();
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(cmc->GetAssetDatabase());
    cmc->LoadBuiltinAssets(BuiltinAssetsDir());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);
    auto rgb = std::make_unique<ComplexRenderGraphBuilder>(*cmc->GetRenderSystem());
    RGTextureHandle final_color_id;
    auto rg = rgb->BuildDefaultRenderGraph(final_color_id);
    cmc->SetRenderGraph(std::move(rg), final_color_id);

    std::filesystem::path path_in_project = "/imported_resources";
    const auto import_begin = std::chrono::steady_clock::now();
    Engine::Importer::ImportExternalResource(mesh_path, path_in_project);
    const auto import_end = std::chrono::steady_clock::now();
    const auto import_ms = std::chrono::duration_cast<std::chrono::milliseconds>(import_end - import_begin).count();
    SDL_LogInfo(
        SDL_LOG_CATEGORY_APPLICATION, "ImportExternalResource took %lld ms.", static_cast<long long>(import_ms)
    );

    auto &main_scene = cmc->GetWorldSystem()->GetMainSceneRef();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading the prefab which has just imported");
    AssetPath prefab_path(
        AssetPath::k_scheme_res, (path_in_project / ("GO_" + mesh_path.stem().string() + ".asset")).generic_string()
    );
    auto prefab_ref = adb->GetNewAssetRef(prefab_path);
    prefab_ref.as<SceneAsset>()->AddToScene(main_scene);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Setting up camera");
    auto &camera_go = main_scene.CreateGameObject();
    camera_go.m_name = "fixed camera";
    Transform transform{};
    transform.SetPosition({0.0f, -3.7f, 2.5f});
    transform.SetRotationEuler(glm::vec3{glm::radians(-30.0f), 0.0f, 0.0f});
    camera_go.SetTransform(transform);
    auto &camera_comp = camera_go.AddComponent<CameraComponent>();
    camera_comp.m_camera->set_aspect_ratio(1.0 * opt.resol_x / opt.resol_y);
    cmc->GetWorldSystem()->SetActiveCamera(camera_comp.GetHandle(), &cmc->GetRenderSystem()->GetCameraManager());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering main loop");
    cmc->LoopFinite(max_frame_count, 0.0f);
    std::filesystem::remove_all(project_path);

    // cmc->LoopFinite(60, 0.0f);
    // auto level_asset = adb->GetNewAssetRef(AssetPath{*adb, path_in_project / "default_level.asset"}).as<LevelAsset>();
    // cmc->GetWorldSystem()->SaveLevelToAsset(*level_asset);
    // AnnoRefl::Archive archive;
    // archive.prepare_save();
    // level_asset->save_asset_to_archive(archive);
    // adb->SaveArchive(archive, adb->GetAssetPath(level_asset->GetGUID()));

    return 0;
}


