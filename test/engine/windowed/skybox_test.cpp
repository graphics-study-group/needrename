#include <Core/FileSystem/DataRoot.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>
#include <numbers>

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Core/Math/Transform.h"
#include "Framework/Import/TextureImportUtils.h"
#include "Framework/MainClass.h"
#include "Render/Asset/Material/MaterialAsset.h"
#include "Render/Asset/Material/MaterialTemplateAsset.h"
#include "Render/Asset/Texture/ImageCubemapAsset.h"
#include "Render/FullRenderSystem.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/UserInterface/GUISystem.h"

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
    AssetsDir() / "skybox" / "skybox_R_tonemapped.png",
    AssetsDir() / "skybox" / "skybox_L_tonemapped.png",
    AssetsDir() / "skybox" / "skybox_F_tonemapped.png",
    AssetsDir() / "skybox" / "skybox_B_tonemapped.png",
    AssetsDir() / "skybox" / "skybox_U_tonemapped.png",
    AssetsDir() / "skybox" / "skybox_D_tonemapped.png"
};

std::pair<MaterialLibraryAsset *, MaterialTemplateAsset *> ConstructMaterial() {
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    auto am = MainClass::GetInstance()->GetAssetManager();
    auto test_asset = am->CreateAsset<MaterialTemplateAsset>();
    auto lib_asset = am->CreateAsset<MaterialLibraryAsset>();
    auto vs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/skybox.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/skybox.frag.asset"});

    test_asset->name = "Skybox";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {Rhi::ImageFormat::R8G8B8A8UNorm};
    using CBP = PipelineProperties::ColorBlendingProperties;
    CBP cbp{};
    mtspp.attachments.color_blending = {cbp};
    mtspp.attachments.depth = Rhi::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<AssetRef>{vs_ref, fs_ref};
    mtspp.depth_stencil.depth_comparator = Rhi::DSComparator::LEqual;

    test_asset->properties = mtspp;

    lib_asset->m_name = "Skybox";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = AssetRef(test_asset);
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
    cmc->LoadBuiltinAssets(BuiltinAssetsDir());
    auto rsys = cmc->GetRenderSystem();
    auto *mi_mng = rsys->GetRenderResourceManager<RenderSystemState::MaterialInstanceManager>();

    auto camera = std::make_shared<Camera>();
    camera->set_aspect_ratio(800.0 / 800.0);
    camera->m_clipping_far = 1e2;

    auto [lib_asset, tpl_asset] = ConstructMaterial();
    auto lib_asset_ref = AssetRef(lib_asset);

    uint32_t width = 1024;
    uint32_t height = 1024;

    std::shared_ptr skybox_texture = Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(),
        Rhi::ImageTexture::ImageTextureDesc{
            .dimensions = 2,
            .width = width,
            .height = height,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 6,
            .format = Rhi::ImageTexture::ITFormat::R8G8B8A8SRGB,
            .is_cube_map = true
        },
        Rhi::SamplerDesc{
            .u_address = Rhi::SamplerDesc::AddressMode::ClampToEdge,
            .v_address = Rhi::SamplerDesc::AddressMode::ClampToEdge,
            .w_address = Rhi::SamplerDesc::AddressMode::ClampToEdge
        },
        "Skybox"
    );

    auto skybox_material_asset = MainClass::GetInstance()->GetAssetManager()->CreateAsset<MaterialAsset>();
    skybox_material_asset->m_library = lib_asset_ref;
    AssetRef skybox_material_asset_ref(skybox_material_asset);
    auto skybox_material_handle = mi_mng->CreateOrReuseFromAsset(skybox_material_asset_ref.GetGUID());
    auto skybox_material = mi_mng->Resolve(skybox_material_handle);
    skybox_material->AssignTexture("skybox", skybox_texture);
    rsys->GetSceneDataManager().SetSkyboxMaterial(skybox_material_handle);

    {
        // Load skybox cubemap
        auto cubemap = MainClass::GetInstance()->GetAssetManager()->CreateAsset<ImageCubemapAsset>();
        Engine::detail::texture_import::LoadImageCubemapAssetFromEquirectangularFile(
            *cubemap, AssetsDir() / "skybox" / "sky_cloudy.png", width, height
        );

        // Engine::Serialization::Archive archive;
        // archive.prepare_save();
        // cubemap->save_asset_to_archive(archive);
        // archive.save_to_file(AssetsDir().string() + "/skybox");

        rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
            *skybox_texture, std::span{cubemap->GetPixelData(), cubemap->GetPixelDataSize()}
        );
        rsys->GetFrameManager().GetSubmissionHelper().ExecuteSubmissionImmediately();
    }

    // Dummy texture for presenting
    RenderGraphBuilder rgb{*rsys};
    auto crt = rgb.RequestRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc{
            .dimensions = 2,
            .width = 800,
            .height = 800,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RTTFormat::R8G8B8A8UNorm
        },
        {}
    );
    auto drt = rgb.RequestRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc{
            .dimensions = 2,
            .width = 800,
            .height = 800,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = RenderTargetTexture::RTTFormat::D32SFLOAT
        },
        {}
    );

    // Read the rendered skybox back so the test can assert that something was
    // drawn. This is the only material in the suite with no blend configuration
    // at all, which is the case a zero colour write mask silently blanked.
    auto readback_buffer = Rhi::DeviceBuffer::CreateUnique(
        rsys->GetAllocatorState(),
        Engine::Rhi::BufferType{Engine::Rhi::BufferTypeBits::ReadbackFromDevice},
        800u * 800u * 4u
    );
    auto *readback_raw = readback_buffer.get();

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Main")
            .AppendColorAttachment(
                {crt, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
            )
            .SetDepthStencilAttachment(
                {drt,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::DontCare,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}}
            )
            .SetPassFunction([rsys, camera](CommandBuffer &cb, const RenderGraph &) -> void {
                glm::mat3 view_matrix = glm::mat3(camera->GetViewMatrix());
                glm::mat4 pv = camera->GetProjectionMatrix() * glm::mat4(view_matrix);
                rsys->GetSceneDataManager().DrawSkybox(
                    cb, rsys->GetFrameManager().GetFrameInFlight(), pv, rsys->GetPresentProvider().GetExtent()
                );
            })
            .WrapRenderPass()
            .Get()
    );

    const auto skybox_readback = rgb.ImportExternalResource(*readback_buffer);
    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Skybox Readback")
            .UseImage(crt, Rhi::MemoryAccessTypeImageBits::TransferRead)
            .UseBuffer(skybox_readback, {Rhi::MemoryAccessTypeBufferBits::TransferWrite})
            .SetAffinity(RenderGraphPassAffinity::Transfer)
            .SetPassFunction([crt, readback_raw](CommandBuffer &cb, const RenderGraph &rg) {
                auto rt = rg.GetInternalTextureResource(crt);
                cb.RecordCopyImageToBuffer(*rt, *readback_raw, Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite);
            })
            .Get()
    );

    auto rg = rgb.BuildRenderGraph();

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
                    euler_angle_rotation = glm::vec3{0.0f, 0.0f, std::numbers::pi_v<float>};
                    break;
                case SDLK_LEFT:
                    // Left
                    euler_angle_rotation.z += std::numbers::pi_v<float> / 4.0f;
                    break;
                case SDLK_RIGHT:
                    // Right
                    euler_angle_rotation.z -= std::numbers::pi_v<float> / 4.0f;
                    break;
                case SDLK_PAGEUP:
                    // Up
                    euler_angle_rotation.x += std::numbers::pi_v<float> / 4.0f;
                    break;
                case SDLK_PAGEDOWN:
                    // Down
                    euler_angle_rotation.x -= std::numbers::pi_v<float> / 4.0f;
                    break;
                }
            }
        }

        if (rsys->StartFrame() == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after retry — skip this frame.
            continue;
        }

        Transform t;
        t.SetPosition({0.0f, 0.0f, 0.0f}).SetRotationEuler(euler_angle_rotation);
        camera->UpdateViewMatrix(t);
        rsys->GetFrameManager().BeginMainCommandBuffer();
        rg->RecordIntoMainCommandBuffer(*rsys);
        rsys->CompleteFrame(*rg->GetInternalTextureResource(crt), Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite);

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    // The skybox is an environment, so a frame that rendered it is never
    // uniformly black. A zero colour write mask produces exactly that, and every
    // other windowed test would still pass.
    const auto *pixels = reinterpret_cast<const uint8_t *>(readback_buffer->GetVMAddress());
    size_t non_black = 0;
    for (size_t i = 0; i < 800u * 800u; i++) {
        const uint8_t r = pixels[i * 4 + 0];
        const uint8_t g = pixels[i * 4 + 1];
        const uint8_t b = pixels[i * 4 + 2];
        if (r != 0 || g != 0 || b != 0) {
            non_black++;
        }
    }

    // A whole-frame dump on request, so a run of this test on one backend can be looked at
    // beside another rather than described.
    if (const char *dump_path = SDL_getenv("SKYBOX_DUMP"); dump_path != nullptr && dump_path[0] != '\0') {
        std::ofstream file(dump_path, std::ios::binary);
        const uint32_t row_bytes = 800u * 3u;
        const uint32_t padding = (4u - (row_bytes % 4u)) % 4u;
        const uint32_t image_bytes = (row_bytes + padding) * 800u;
        const uint32_t file_size = 54u + image_bytes;
        const auto byte_at = [](uint32_t value, int shift) {
            return static_cast<uint8_t>((value >> shift) & 0xFFu);
        };
        const uint8_t header[54] = {
            'B', 'M', byte_at(file_size, 0), byte_at(file_size, 8), byte_at(file_size, 16), byte_at(file_size, 24),
            0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
            byte_at(800u, 0), byte_at(800u, 8), byte_at(800u, 16), byte_at(800u, 24),
            byte_at(800u, 0), byte_at(800u, 8), byte_at(800u, 16), byte_at(800u, 24),
            1, 0, 24, 0, 0, 0, 0, 0,
            byte_at(image_bytes, 0), byte_at(image_bytes, 8), byte_at(image_bytes, 16), byte_at(image_bytes, 24),
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        file.write(reinterpret_cast<const char *>(header), sizeof(header));
        for (uint32_t y = 0; y < 800u; y++) {
            const uint32_t source_row = 799u - y;
            for (uint32_t x = 0; x < 800u; x++) {
                const size_t at = (static_cast<size_t>(source_row) * 800u + x) * 4u;
                const char bgr[3] = {
                    static_cast<char>(pixels[at + 2]), static_cast<char>(pixels[at + 1]),
                    static_cast<char>(pixels[at + 0])
                };
                file.write(bgr, 3);
            }
            for (uint32_t pad = 0; pad < padding; pad++) {
                file.put('\0');
            }
        }
        std::cout << "Skybox dump written to " << dump_path << std::endl;
    }

    if (non_black == 0) {
        std::cerr << "Skybox test FAILED: the rendered frame is entirely black, so "
                     "the skybox pipeline wrote no colour."
                  << std::endl;
        return 1;
    }

    std::cout << "Skybox test PASSED: " << non_black << " of 640000 pixels are not black." << std::endl;
    return 0;
}



