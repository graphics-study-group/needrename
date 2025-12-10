#include "MainClass.h"

#include "rendering.hpp"
#include "ui.hpp"

#include <Core/Math/Transform.h>

constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;

using namespace Engine;

int main() {
    StartupOptions opt{.resol_x = WIDTH, .resol_y = HEIGHT, .title = "NPR Shading Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto rsys = cmc->GetRenderSystem();
    auto gsys = cmc->GetGUISystem();
    gsys->CreateVulkanBackend(*rsys, vk::Format::eR8G8B8A8Unorm);

    Transform transform{};
    transform.SetPosition({0.0f, 1.0f, 0.0f});
    transform.SetRotationEuler(glm::vec3{0.0, 0.0, 3.1415926});
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(WIDTH * 1.0f / HEIGHT);
    camera->UpdateViewMatrix(transform);
    rsys->GetCameraManager().RegisterCamera(camera);
    rsys->GetCameraManager().SetActiveCameraIndex(camera->m_display_id);

    auto [color, depth] = MakeRenderTargetTextures(*rsys, std::make_pair(WIDTH, HEIGHT));
    auto material_library_asset = GetMaterialLibraryAsset();
    auto material_library = std::make_unique<MaterialLibrary>(*rsys);
    material_library->Instantiate(*material_library_asset);

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
