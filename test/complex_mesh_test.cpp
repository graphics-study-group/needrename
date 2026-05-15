#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stb_image.h>

#include "Asset/Material/MaterialAsset.h"
#include "Core/Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include "UserInterface/GUISystem.h"
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Loader/ObjLoader.h>
#include <Asset/Mesh/MeshAsset.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>

#include "cmake_config.h"

using namespace Engine;
namespace sch = std::chrono;

class MeshComponentFromFile : public StaticMeshComponent {
    Transform transform;

    struct UniformData {
        glm::vec4 specular;
        glm::vec4 ambient;
    };

    UniformData m_uniform_data{glm::vec4{0.5, 0.5, 0.5, 4.0}, glm::vec4{0.1, 0.1, 0.1, 1.0}};
    std::vector<GUID> m_material_guids{};

    void LoadMesh(std::filesystem::path mesh) {
        ObjLoader loader{};
        ImportResult imported = loader.LoadObjInMemory(mesh);
        if (!imported.mesh_asset.IsValid()) {
            throw std::runtime_error("ObjLoader did not return a valid mesh asset.");
        }
        m_mesh_asset = imported.mesh_asset;
        m_material_assets = imported.mesh_material_assets;
        m_material_guids.clear();
        for (const auto &mat_ref : m_material_assets) {
            m_material_guids.push_back(mat_ref.GetGUID());
        }

        assert(m_mesh_asset.IsValid());
    }

public:
    MeshComponentFromFile(const GameObject &parent) : StaticMeshComponent(parent), transform() {
    }

    void LoadFile(std::filesystem::path mesh_file_name) {
        LoadMesh(mesh_file_name);
    }

    Transform GetWorldTransform() const override {
        return transform;
    }

    void UpdateUniformData(float spec_r, float spec_g, float spec_b, float spec_coef) {
        uint8_t identity =
            (fabs(spec_r - m_uniform_data.specular.r) < 1e-3) + (fabs(spec_g - m_uniform_data.specular.g) < 1e-3)
            + (fabs(spec_b - m_uniform_data.specular.b) < 1e-3) + (fabs(spec_coef - m_uniform_data.specular.a) < 1e-3);
        if (identity == 4) return;

        m_uniform_data.specular = glm::vec4{spec_r, spec_g, spec_b, spec_coef};
        auto &rsys = *MainClass::GetInstance()->GetRenderSystem();
        auto &mat_mng = rsys.GetRenderResourceManager<RenderSystemState::MaterialInstanceManager>();
        for (auto guid : m_material_guids) {
            auto handle = mat_mng.CreateOrReuseFromAsset(guid);
            auto mat_ptr = mat_mng.Resolve(handle);
            if (mat_ptr) {
                mat_ptr->AssignVectorVariable("Material::ambient_color", m_uniform_data.ambient);
                mat_ptr->AssignVectorVariable("Material::specular_color", m_uniform_data.specular);
            }
        }
    }
};

struct {
    float zenith, azimuth;
    float r, g, b, coef;
} g_SceneData{M_PI_2, M_PI_2 * 2, 0.5f, 0.5f, 0.5f, 4.0f};

glm::vec3 GetCartesian(float zenith, float azimuth) {
    static constexpr float RADIUS = 2.0f;
    return glm::vec3{RADIUS * sin(zenith) * cos(azimuth), RADIUS * sin(zenith) * sin(azimuth), RADIUS * cos(zenith)};
}

void PrepareGui() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    ImGui::SetNextWindowPos({10, 10});
    ImGui::SetNextWindowSize(ImVec2{300, 300});
    ImGui::Begin("Configuration", nullptr, flags);
    ImGui::SliderAngle("Zenith", &g_SceneData.zenith, -180.0f, 180.0f);
    ImGui::SliderAngle("Azimuth", &g_SceneData.azimuth, 0.0f, 360.0f);

    glm::vec3 light_source = GetCartesian(g_SceneData.zenith, g_SceneData.azimuth);
    ImGui::Text("Coordinate: (%.3f, %.3f, %.3f).", light_source.x, light_source.y, light_source.z);

    ImGui::Separator();

    ImGui::ColorPicker3("Specular color", &g_SceneData.r);
    ImGui::SliderFloat("Specular strength", &g_SceneData.coef, 0.0f, 64.0f);
    ImGui::End();
}

void SubmitSceneData(std::shared_ptr<RenderSystem> rsys, uint32_t id) {
    rsys->GetSceneDataManager().SetLightDirectionalNonShadowCasting(
        0, GetCartesian(g_SceneData.zenith, g_SceneData.azimuth), glm::vec3{1.0, 1.0, 1.0}
    );
    rsys->GetSceneDataManager().SetLightCountNonShadowCasting(1);
}

void SubmitMaterialData(MeshComponentFromFile *mesh) {
    mesh->UpdateUniformData(g_SceneData.r, g_SceneData.g, g_SceneData.b, g_SceneData.coef);
}

int main(int argc, char **argv) {
    SDL_Init(SDL_INIT_VIDEO);

    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto asys = cmc->GetAssetManager();
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();

    auto gsys = cmc->GetGUISystem();
    gsys->CreateVulkanBackend(*rsys, ImageUtils::GetVkFormat(Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm));

    RenderGraphBuilder rgb{*rsys};
    auto rg{rgb.BuildDefaultRenderGraph(1280, 720, gsys.get())};

    auto &scene = cmc->GetWorldSystem()->GetMainSceneRef();
    // Setup mesh
    std::filesystem::path mesh_path{std::string(ENGINE_ASSETS_DIR) + "/red_brick_sphere/red_brick_sphere.obj"};
    auto &go = scene.CreateGameObject();
    auto &tmc = scene.CreateComponent<MeshComponentFromFile>(go);
    tmc.LoadFile(mesh_path);
    tmc.Awake();

    // Setup camera
    Transform transform{};
    transform.SetPosition({0.0f, 3.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(1920.0 / 1080.0);
    camera->UpdateViewMatrix(transform);
    rsys->GetCameraManager().RegisterCamera(camera);
    rsys->GetCameraManager().SetActiveCameraIndex(camera->m_display_id);

    uint64_t frame_count = 0;
    uint64_t start_timer = SDL_GetPerformanceCounter();
    while (++frame_count) {
        bool quited = false;
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
            gsys->ProcessEvent(&event);
        }
        if (quited) break;

        gsys->PrepareGUI();

        PrepareGui();

        SubmitSceneData(rsys, rsys->GetFrameManager().GetFrameInFlight());
        SubmitMaterialData(&tmc);

        tmc.PreRenderUpdate();

        auto index = rsys->StartFrame();
        rg->Execute();
        auto color = rg->GetInternalTextureResource(0);
        rsys->CompleteFrame(*color, color->GetTextureDescription().width, color->GetTextureDescription().height);

        SDL_Delay(5);

        if ((int64_t)frame_count >= max_frame_count) break;
    }
    uint64_t end_timer = SDL_GetPerformanceCounter();
    uint64_t duration = end_timer - start_timer;
    double duration_time = 1.0 * duration / SDL_GetPerformanceFrequency();
    SDL_LogInfo(
        0,
        "Took %lf seconds for %llu frames (avg. %lf fps).",
        duration_time,
        frame_count,
        frame_count * 1.0 / duration_time
    );
    rsys->WaitForIdle();

    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
    return 0;
}
