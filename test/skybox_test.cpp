#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Texture/ImageCubemapAsset.h"
#include "UserInterface/GUISystem.h"
#include "MainClass.h"
#include "Core/Math/Transform.h"
#include "Render/FullRenderSystem.h"

#include "cmake_config.h"
#include <ext/matrix_transform.hpp>
#include <iostream>

using namespace Engine;
namespace sch = std::chrono;

/**
 * We will align the system to the world system for now, which means that
 * X+ -> right, Y+ -> front, Z+ -> up, etc. Refer to 
 * https://docs.vulkan.org/spec/latest/chapters/textures.html#_cube_map_face_selection
 * for how to organize the cubemap faces.
 * 
 * For short, assuming your faces are generated with the `split.py` from an ERP image:
 * - Front face (Y+): no adjustment;
 * - Back face (Y-): rotated for 180 degrees;
 * - Right face (X+): rotated counterclockwise for 90 degrees;
 * - Left face (X-): rotated clockwise for 90 degrees;
 * - Up face (Z+): no adjustment;
 * - Dow face (Z-): rotated for 180 degrees;
 */
const std::array<std::filesystem::path, 6> CUBEMAP_FACES = {
    std::filesystem::path{ENGINE_ASSETS_DIR} / "skybox" / "skybox_R_tonemapped.png",
    std::filesystem::path{ENGINE_ASSETS_DIR} / "skybox" / "skybox_L_tonemapped.png",
    std::filesystem::path{ENGINE_ASSETS_DIR} / "skybox" / "skybox_F_tonemapped.png",
    std::filesystem::path{ENGINE_ASSETS_DIR} / "skybox" / "skybox_B_tonemapped.png",
    std::filesystem::path{ENGINE_ASSETS_DIR} / "skybox" / "skybox_U_tonemapped.png",
    std::filesystem::path{ENGINE_ASSETS_DIR} / "skybox" / "skybox_D_tonemapped.png"
};


std::pair<std::shared_ptr<MaterialLibraryAsset>, std::shared_ptr<MaterialTemplateAsset>> ConstructMaterial() {
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(
        MainClass::GetInstance()->GetAssetDatabase()
    );
    auto test_asset = std::make_shared<MaterialTemplateAsset>();
    auto lib_asset = std::make_shared<MaterialLibraryAsset>();
    auto vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/skybox.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef({*adb, "~/shaders/skybox.frag.asset"});
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(vs_ref);
    MainClass::GetInstance()->GetAssetManager()->LoadAssetImmediately(fs_ref);

    test_asset->name = "Skybox";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {ImageUtils::ImageFormat::R8G8B8A8UNorm};
    using CBP = PipelineProperties::ColorBlendingProperties;
    CBP cbp{};
    mtspp.attachments.color_blending = {cbp};
    mtspp.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<std::shared_ptr<AssetRef>>{vs_ref, fs_ref};
    mtspp.depth_stencil.depth_comparator = PipelineUtils::DSComparator::LEqual;

    test_asset->properties = mtspp;

    lib_asset->m_name = "Skybox";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = std::make_shared<AssetRef>(test_asset);
    lib_asset->material_bundle["SKYBOX"] = ref;

    return std::make_pair(lib_asset, test_asset);
}

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 800, .resol_y = 800, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));
    auto rsys = cmc->GetRenderSystem();
    
    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(800.0 / 800.0);
    camera->m_clipping_far = 1e2;

    auto [lib_asset, tpl_asset] = ConstructMaterial();
    auto lib = std::make_shared<MaterialLibrary>(*rsys);
    lib->Instantiate(*lib_asset);

    std::shared_ptr skybox_texture = ImageTexture::CreateUnique(
        *rsys,
        ImageTexture::ImageTextureDesc{
            .dimensions = 2,
            .width = 512,
            .height = 512,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 6,
            .format = ImageTexture::ITFormat::R8G8B8A8SRGB,
            .is_cube_map = true
        },
        ImageUtils::SamplerDesc{
            .u_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge,
            .v_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge,
            .w_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge
        },
        "Skybox"
    );

    auto skybox_material = std::make_shared<MaterialInstance>(*rsys, *lib);
    skybox_material->AssignTexture("skybox", skybox_texture);
    rsys->GetSceneDataManager().SetSkyboxMaterial(skybox_material);

    {
        // Load skybox cubemap
        auto cubemap = std::make_shared<ImageCubemapAsset>();
        cubemap->LoadFromFile(std::filesystem::path{ENGINE_ASSETS_DIR} / "skybox" / "sky_cloudy.png", 512, 512);
        // cubemap->LoadFromFile(CUBEMAP_FACES);
        
        // Engine::Serialization::Archive archive;
        // archive.prepare_save();
        // cubemap->save_asset_to_archive(archive);
        // archive.save_to_file(std::string(ENGINE_ASSETS_DIR) + "/skybox");

        rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
            *skybox_texture,
            cubemap->GetPixelData(),
            cubemap->GetPixelDataSize()
        );
        rsys->GetFrameManager().GetSubmissionHelper().ExecuteSubmissionImmediately();
    }

    // Dummy texture for presenting
    auto rt = RenderTargetTexture::CreateUnique(
        *rsys,
        RenderTargetTexture::RenderTargetTextureDesc{
            .dimensions = 2,
            .width = 800,
            .height = 800,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RTTFormat::R8G8B8A8UNorm
        },
        ImageUtils::SamplerDesc{},
        "Dummy render texture"
    );
    auto drt = RenderTargetTexture::CreateUnique(
        *rsys,
        RenderTargetTexture::RenderTargetTextureDesc{
            .dimensions = 2,
            .width = 800,
            .height = 800,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RTTFormat::D32SFLOAT
        },
        ImageUtils::SamplerDesc{},
        "Dummy render texture"
    );

    std::array color_attachments {
        vk::RenderingAttachmentInfo{
            rt->GetImageView(),
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            nullptr, vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}
        }
    };

    vk::RenderingAttachmentInfo depth_attachment {
        drt->GetImageView(),
        vk::ImageLayout::eDepthAttachmentOptimal,
        vk::ResolveModeFlagBits::eNone,
        nullptr, vk::ImageLayout::eUndefined,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eDontCare,
        vk::ClearDepthStencilValue{1.0f, 0U}
    };

    std::array barriers {
        vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            rt->GetImage(),
            vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eColor,
                0, vk::RemainingMipLevels,
                0, vk::RemainingArrayLayers
            }
        },
        vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eTopOfPipe,
            vk::AccessFlagBits2::eNone,
            vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            drt->GetImage(),
            vk::ImageSubresourceRange{
                vk::ImageAspectFlagBits::eDepth,
                0, vk::RemainingMipLevels,
                0, vk::RemainingArrayLayers
            }
        }
    };

    bool quited{false};
    glm::vec3 euler_angle_rotation{};
    int64_t current_frame = 0;

    while (current_frame < max_frame_count) {
        current_frame++;

        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            case SDL_EVENT_KEY_UP:
                auto keycode = event.key.key;
                switch (keycode) {
                    case SDLK_UP:
                    // Front
                    euler_angle_rotation = glm::vec3{0.0f, 0.0f, 0.0f};
                    break;
                    case SDLK_DOWN:
                    // Back
                    euler_angle_rotation = glm::vec3{0.0f, 0.0f, M_PI};
                    break;
                    case SDLK_LEFT:
                    // Left
                    euler_angle_rotation.z += M_PI_4;
                    break;
                    case SDLK_RIGHT:
                    // Right
                    euler_angle_rotation.z -= M_PI_4;
                    break;
                    case SDLK_PAGEUP:
                    // Up
                    euler_angle_rotation.x += M_PI_4;
                    break;
                    case SDLK_PAGEDOWN:
                    // Down
                    euler_angle_rotation.x -= M_PI_4;
                    break;
                }
            }
        }

        rsys->StartFrame();
        auto cb = rsys->GetFrameManager().GetRawMainCommandBuffer();
        vk::CommandBufferBeginInfo cbbi{};
        cb.begin(cbbi);
        cb.pipelineBarrier2(vk::DependencyInfo{
            vk::DependencyFlags{}, {}, {}, barriers
        });
        cb.setViewport(0, {vk::Viewport{0, 0, 800, 800, 0.0f, 1.0f}});
        cb.setScissor(0, {vk::Rect2D{{0, 0}, {800, 800}}});
        cb.beginRendering(vk::RenderingInfo{
            vk::RenderingFlags{},
            vk::Rect2D{{0, 0}, {800, 800}},
            1, 0,
            color_attachments,
            &depth_attachment
        });
        Transform t;
        t.SetPosition({0.0f, 0.0f, 0.0f}).SetRotationEuler(euler_angle_rotation);
        camera->UpdateViewMatrix(t);
        rsys->GetSceneDataManager().DrawSkybox(
            cb,
            rsys->GetFrameManager().GetFrameInFlight(),
            camera->GetProjectionMatrix() * camera->GetViewMatrix()
        );
        cb.endRendering();
        cb.end();
        rsys->GetFrameManager().SubmitMainCommandBuffer();
        rsys->CompleteFrame(*rt, 800, 800);

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    return 0;
}
