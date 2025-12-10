#include "MainClass.h"

#include "rendering.hpp"
#include "ui.hpp"

#include <Core/Math/Transform.h>
#include <Framework/world/WorldSystem.h>
#include <Framework/component/RenderComponent/ObjTestMeshComponent.h>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;

using namespace Engine;


class ObjMeshComponent : public ObjTestMeshComponent {
public:
    ObjMeshComponent(
        std::weak_ptr <GameObject> go,
        std::filesystem::path mesh_file_name,
        std::shared_ptr<MaterialInstance> instance
    ) : ObjTestMeshComponent(mesh_file_name, go) {
        auto system = m_system.lock();

        for (size_t i = 0; i < m_submeshes.size(); i++) {
            m_materials.push_back(instance);
        }
    }
};

int main() {
    StartupOptions opt{.resol_x = WIDTH, .resol_y = HEIGHT, .title = "NPR Shading Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto rsys = cmc->GetRenderSystem();
    auto gsys = cmc->GetGUISystem();
    gsys->CreateVulkanBackend(*rsys, vk::Format::eR8G8B8A8Unorm);

    Transform transform{};
    transform.SetPosition({0.0f, 0.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 0.0});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(WIDTH * 1.0f / HEIGHT);
    camera->UpdateViewMatrix(transform);
    rsys->GetCameraManager().RegisterCamera(camera);
    rsys->GetCameraManager().SetActiveCameraIndex(camera->m_display_id);

    auto [color, depth] = MakeRenderTargetTextures(*rsys, std::make_pair(WIDTH, HEIGHT));
    auto material_library_asset = GetMaterialLibraryAsset();
    auto material_library = std::make_unique<MaterialLibrary>(*rsys);
    material_library->Instantiate(*material_library_asset);

    auto idesc = ImageTexture::ImageTextureDesc{
        .dimensions = 2, .width = 16, .height = 16, .depth = 1,
        .mipmap_levels = 1, .array_layers = 1,
        .format = ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
        .is_cube_map = false
    };
    std::shared_ptr blank_color_gray = Engine::ImageTexture::CreateUnique(*rsys, idesc, Texture::SamplerDesc{}, "Blank color gray");
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_gray, {0.7f, 0.7f, 0.7f, 0.0f});
    std::vector control_points {
        RampControlPoint{0.0f, 0, 0, 0},
        RampControlPoint{0.5f, 255, 255, 255}
    };
    std::shared_ptr ramp_map = CreateRampMapTexture(*rsys, 128);
    auto data = FillRampMap(128, control_points);
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
        *ramp_map,
        reinterpret_cast<std::byte *>(data.data()),
        data.size()
    );
    auto material_instance = std::make_shared<MaterialInstance>(*rsys, *material_library);
    material_instance->AssignVectorVariable("Material::rim_light_color", glm::vec4{1.0f, 1.0f, 1.0f, 0.0f});
    material_instance->AssignTexture("base_texture", blank_color_gray);
    material_instance->AssignTexture("ramp_texture", ramp_map);

    auto shpere_go = cmc->GetWorldSystem()->CreateGameObject<GameObject>();
    shpere_go->GetTransformRef().SetScale({0.5f, 0.5f, 0.5f}).SetPosition({0.0f, 2.0f, 0.0f});
    auto sphere_mesh_comp = std::make_shared<ObjMeshComponent>(
        shpere_go,
        std::filesystem::path{std::string(ENGINE_ASSETS_DIR) + "/meshes/sphere.obj"},
        material_instance
    );
    rsys->GetRendererManager().RegisterRendererComponent(sphere_mesh_comp);

    auto rg = BuildRenderGraph(*rsys, *color, *depth, cmc->GetGUISystem().get());

    bool quited = false;
    int64_t frame_count = 0;
    while (++frame_count) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            }
            gsys->ProcessEvent(&event);
        }

        auto index = rsys->StartFrame();
        assert(index < 3);

        gsys->PrepareGUI();
        DrawGUI();
        UpdateSceneData(*rsys);

        rg.Execute();
        rsys->GetFrameManager().StageCopyComposition(color->GetImage());
        rsys->CompleteFrame();

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();
    return 0;
}
