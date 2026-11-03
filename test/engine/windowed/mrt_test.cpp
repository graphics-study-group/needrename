#include <Core/FileSystem/DataRoot.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Asset/AssetManager/AssetManager.h"
#include "Core/Functional/SDLWindow.h"
#include "Framework/Component/RenderComponent/StaticMeshComponent.h"
#include "Framework/MainClass.h"
#include "Render/Asset/Material/MaterialTemplateAsset.h"
#include "Render/Asset/Mesh/PlaneMeshAsset.h"
#include "Render/Asset/Texture/Image2DTextureAsset.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/Renderer/StaticHomogeneousMesh.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/UserInterface/GUISystem.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>

#include "cmake_config.h"
#include <iostream>

using namespace Engine;
namespace sch = std::chrono;

constexpr glm::mat4 EYE4 = glm::mat4(1.0f);

namespace {
    /// @brief Whether any frame produced a non-zero pixel in the read-back target.
    ///
    /// Every other windowed test asserts nothing about what was drawn, so a
    /// pipeline that writes no colour - which a zero colour write mask does
    /// silently - passes them all. This one looks.
    bool g_frame_has_colour = false;
} // namespace

namespace {
    /// @brief Write a tight 24-bit BMP of a read-back colour target.
    ///
    /// The dump exists so a frame rendered on one backend can be looked at rather than
    /// described: the same test on another backend produces a file that can be compared with
    /// this one.
    void DumpReadbackAsBmp(const char *path, const std::byte *pixels, uint32_t width, uint32_t height) {
        if (pixels == nullptr) {
            std::cerr << "MRT dump: the read-back buffer has no address." << std::endl;
            return;
        }
        const uint32_t row_bytes = width * 3u;
        const uint32_t padding = (4u - (row_bytes % 4u)) % 4u;
        const uint32_t image_bytes = (row_bytes + padding) * height;
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "MRT dump: cannot open " << path << std::endl;
            return;
        }
        const uint32_t file_size = 54u + image_bytes;
        const uint8_t header[54] = {
            'B', 'M',
            static_cast<uint8_t>(file_size), static_cast<uint8_t>(file_size >> 8),
            static_cast<uint8_t>(file_size >> 16), static_cast<uint8_t>(file_size >> 24),
            0, 0, 0, 0,
            54, 0, 0, 0,
            40, 0, 0, 0,
            static_cast<uint8_t>(width), static_cast<uint8_t>(width >> 8),
            static_cast<uint8_t>(width >> 16), static_cast<uint8_t>(width >> 24),
            static_cast<uint8_t>(height), static_cast<uint8_t>(height >> 8),
            static_cast<uint8_t>(height >> 16), static_cast<uint8_t>(height >> 24),
            1, 0,
            24, 0,
            0, 0, 0, 0,
            static_cast<uint8_t>(image_bytes), static_cast<uint8_t>(image_bytes >> 8),
            static_cast<uint8_t>(image_bytes >> 16), static_cast<uint8_t>(image_bytes >> 24),
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        file.write(reinterpret_cast<const char *>(header), sizeof(header));
        // Bottom-up, as BMP stores it.
        for (uint32_t y = 0; y < height; y++) {
            const uint32_t source_row = height - 1u - y;
            for (uint32_t x = 0; x < width; x++) {
                const size_t at = (static_cast<size_t>(source_row) * width + x) * 4u;
                const char bgr[3] = {
                    static_cast<char>(std::to_integer<uint8_t>(pixels[at + 2])),
                    static_cast<char>(std::to_integer<uint8_t>(pixels[at + 1])),
                    static_cast<char>(std::to_integer<uint8_t>(pixels[at + 0]))
                };
                file.write(bgr, 3);
            }
            for (uint32_t pad = 0; pad < padding; pad++) {
                file.put('\0');
            }
        }
        std::cout << "MRT dump written to " << path << std::endl;
    }
} // namespace

struct LowerPlaneMeshAsset : public PlaneMeshAsset {

    constexpr static std::array<float, 12> REPLACEMENT_POSITION = {
        1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 0.5f, -1.0f, 1.0f, 0.5f, -1.0f, -1.0f, 0.5f
    };

    LowerPlaneMeshAsset() : PlaneMeshAsset() {
        assert(this->m_submeshes[0].positions.buffer_size == REPLACEMENT_POSITION.size() * sizeof(float));
        // Replace position buffer
        std::memcpy(
            reinterpret_cast<std::byte *>(m_submeshes[0].m_vertex_attributes.data())
                + this->m_submeshes[0].positions.buffer_offset,
            reinterpret_cast<const std::byte *>(REPLACEMENT_POSITION.data()),
            this->m_submeshes[0].positions.buffer_size
        );
        // Reverse vertex normal
        float *nb{
            reinterpret_cast<float *>(m_submeshes[0].m_vertex_attributes.data() + m_submeshes[0].normal.buffer_offset)
        };
        float *ne{reinterpret_cast<float *>(
            m_submeshes[0].m_vertex_attributes.data() + m_submeshes[0].normal.buffer_offset
            + m_submeshes[0].normal.buffer_size
        )};
        for (float *i = nb; i < ne; i += 3) {
            *(i + 2) = -1.0f;
        };
    }
};

std::pair<MaterialLibraryAsset *, MaterialTemplateAsset *> ConstructMaterial() {
    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    auto am = MainClass::GetInstance()->GetAssetManager();
    auto test_asset = am->CreateAsset<MaterialTemplateAsset>();
    auto test_lib_asset = am->CreateAsset<MaterialLibraryAsset>();
    auto vs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/debug_writethrough.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/debug_writethrough_mrt.frag.asset"});

    test_asset->name = "Writethrough";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {
        Rhi::ImageFormat::R8G8B8A8UNorm,
        Rhi::ImageFormat::R8G8B8A8UNorm,
        Rhi::ImageFormat::R8G8B8A8UNorm,
        Rhi::ImageFormat::R8G8B8A8UNorm
    };
    using CBP = PipelineProperties::ColorBlendingProperties;
    CBP cbp;
    cbp.color_op = cbp.alpha_op = CBP::BlendOperation::None;
    mtspp.attachments.color_blending = {cbp, cbp, cbp, cbp};
    mtspp.attachments.depth = Rhi::ImageFormat::D32SFLOAT;
    mtspp.shaders.shaders = std::vector<AssetRef>{vs_ref, fs_ref};

    test_asset->properties = mtspp;

    test_lib_asset->m_name = "MRT Writethrough";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = AssetRef(test_asset);
    test_lib_asset->material_bundle[""] = ref;

    return std::make_pair(test_lib_asset, test_asset);
}

std::array<RGTextureHandle, 4> g_color_handles = {};

auto BuildRenderGraph(
    RenderSystem *rsys,
    Rhi::DeviceBuffer *readback,
    RenderTargetTexture *color_1,
    RenderTargetTexture *color_2,
    RenderTargetTexture *color_3,
    RenderTargetTexture *color_4,
    RenderTargetTexture *depth,
    MaterialInstance *material,
    IVertexBasedRenderer *mesh
) {
    RenderGraphBuilder rgb{*rsys};
    auto c1 = rgb.ImportExternalResource(*color_1);
    auto c2 = rgb.ImportExternalResource(*color_2);
    auto c3 = rgb.ImportExternalResource(*color_3);
    auto c4 = rgb.ImportExternalResource(*color_4);
    auto b1 = rgb.ImportExternalResource(*readback);
    auto d0 = rgb.ImportExternalResource(*depth);

    g_color_handles = {c1, c2, c3, c4};

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Main")
            .AppendColorAttachment(
                {c1,
                 Engine::Rhi::TextureSubresourceRange::GetSingleRange(),
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store}
            )
            .AppendColorAttachment(
                {c2,
                 Engine::Rhi::TextureSubresourceRange::GetSingleRange(),
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store}
            )
            .AppendColorAttachment(
                {c3,
                 Engine::Rhi::TextureSubresourceRange::GetSingleRange(),
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store}
            )
            .AppendColorAttachment(
                {c4,
                 Engine::Rhi::TextureSubresourceRange::GetSingleRange(),
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store}
            )
            .SetDepthStencilAttachment(
                {d0,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::DontCare,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}}
            )
            .SetPassFunction([rsys, color_1, color_2, color_3, color_4, depth, material, mesh](
                                 CommandBuffer &cb, const RenderGraph &
                             ) {
                const auto extent = rsys->GetPresentProvider().GetExtent();
                cb.SetupViewport(
                    static_cast<float>(extent.width), static_cast<float>(extent.height), {{0, 0}, {extent.width, extent.height}}
                );
                cb.BindSceneResources(rsys->GetSceneDataManager());
                cb.BindCameraResources(rsys->GetCameraManager());

                PipelineRuntimeInfo pri{};
                pri.va.SetAttribute(VertexAttributeSemantic::Position, VertexAttributeType::SFloat32x3);
                pri.va.SetAttribute(VertexAttributeSemantic::Color, VertexAttributeType::SFloat32x3);
                pri.va.SetAttribute(VertexAttributeSemantic::Normal, VertexAttributeType::SFloat32x3);
                pri.va.SetAttribute(VertexAttributeSemantic::Texcoord0, VertexAttributeType::SFloat32x2);
                pri.color_attachment_format[0] = color_1->GetTextureDescription().format;
                pri.color_attachment_format[1] = color_2->GetTextureDescription().format;
                pri.color_attachment_format[2] = color_3->GetTextureDescription().format;
                pri.color_attachment_format[3] = color_4->GetTextureDescription().format;
                pri.color_attachment_format[4] = Rhi::ImageFormat::UNDEFINED;
                pri.depth_stencil_attachment_format = depth->GetTextureDescription().format;

                auto tpl = material->GetLibrary().FindMaterialTemplate("", pri);
                assert(tpl);
                cb.BindMaterial(*material, *tpl);
                // Push model matrix...
                cb.GetEncoder().PushConstants(std::as_bytes(std::span{&EYE4, 1}));
                cb.DrawMesh(*mesh);
            })
            .WrapRenderPass()
            .Get()
    );

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Transfer")
            .UseBuffer(b1, {Rhi::MemoryAccessTypeBufferBits::TransferWrite})
            .UseImage(c1, Rhi::MemoryAccessTypeImageBits::TransferRead)
            .SetAffinity(RenderGraphPassAffinity::Transfer)
            .SetPassFunction([readback, c1](CommandBuffer &cb, const RenderGraph &rg) {
                auto rt = rg.GetInternalTextureResource(c1);
                // The seam's readback: one call that records the transition, the copy and
                // the transition back, so the pass says what it wants rather than how.
                cb.RecordCopyImageToBuffer(*rt, *readback, Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite);
            })
            .Get()
    );

    return rgb.BuildRenderGraph();
}

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }
    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Vulkan Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(BuiltinAssetsDir());

    auto rsys = cmc->GetRenderSystem();
    auto amg = cmc->GetAssetManager();

    // Prepare material
    auto [test_library_asset, test_template_asset] = ConstructMaterial();
    auto *ml_mng = rsys->GetRenderResourceManager<RenderSystemState::MaterialLibraryManager>();

    auto test_library_handle = ml_mng->CreateOrReuseFromAsset(test_library_asset->GetGUID());
    auto test_library = ml_mng->Resolve(test_library_handle);
    auto test_material_instance = std::make_unique<MaterialInstance>(*rsys, test_library_handle);

    // Prepare mesh
    auto test_mesh_asset = amg->CreateAsset<LowerPlaneMeshAsset>();
    auto test_mesh_asset_ref = AssetRef(test_mesh_asset);
    auto mesh_resource = std::make_shared<StaticMeshResource>(test_mesh_asset->GetGUID());
    StaticHomogeneousMesh test_mesh{0, mesh_resource.get()};
    mesh_resource->Submit(rsys->GetAllocatorState(), rsys->GetFrameManager().GetSubmissionHelper());

    // Submit scene data
    rsys->GetCameraManager().WriteCameraMatrices(glm::mat4{1.0f}, glm::mat4{1.0f});
    rsys->GetSceneDataManager().SetLightDirectionalNonShadowCasting(
        0, glm::vec3{-5.0f, -5.0f, -5.0f}, glm::vec3{1.0, 1.0, 1.0}
    );
    rsys->GetSceneDataManager().SetLightCountNonShadowCasting(1);

    // Prepare attachments
    RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT,
        .multisample = 1,
        .is_cube_map = false
    };
    auto depth = RenderTargetTexture::CreateUnique(
        rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Depth Attachment"
    );
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    std::array colors{
        RenderTargetTexture::CreateUnique(
            rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Attachment (Position)"
        ),
        RenderTargetTexture::CreateUnique(
            rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Attachment (Vertex color)"
        ),
        RenderTargetTexture::CreateUnique(
            rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Attachment (Normal)"
        ),
        RenderTargetTexture::CreateUnique(
            rsys->GetDeviceContext(), desc, Rhi::Texture::SamplerDesc{}, "Color Attachment (Texcoord)"
        )
    };
    auto readback_buffer = Rhi::DeviceBuffer::CreateUnique(
        rsys->GetAllocatorState(),
        Engine::Rhi::BufferType{Engine::Rhi::BufferTypeBits::CopyTo, Engine::Rhi::BufferTypeBits::CopyFrom},
        colors[0]->CalculateStagingBufferSizeNoMipmap()
    );

    auto asys = cmc->GetAssetManager();

    auto rg{BuildRenderGraph(
        rsys.get(),
        readback_buffer.get(),
        colors[0].get(),
        colors[1].get(),
        colors[2].get(),
        colors[3].get(),
        depth.get(),
        test_material_instance.get(),
        &test_mesh
    )};

    bool quited = false;
    int color = 0;
    while (max_frame_count--) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quited = true;
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key == SDLK_G) {
                    color = (color + 1) % 4;
                }
            }
        }

        auto index = rsys->StartFrame();
        if (index == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after retry — skip this frame.
            continue;
        }

        // Registration must not wait for the device: it queues a callback and
        // returns. A blocking implementation would wait for the frame's work to
        // land here, which on this hardware is milliseconds - an order of
        // magnitude above the threshold below.
        const auto register_start = sch::steady_clock::now();

        rsys->GetFrameManager().RegisterReadbackCallback(*readback_buffer, [](Rhi::DeviceBuffer &in) {
            auto byte = in.GetVMAddress();

            // A whole-frame dump on request: this is what lets a run of the same test on
            // another backend be looked at rather than described. The read-back target is the
            // first of the four MRT attachments, so the image is that attachment.
            if (const char *dump_path = SDL_getenv("MRT_DUMP"); dump_path != nullptr && dump_path[0] != '\0') {
                DumpReadbackAsBmp(dump_path, byte, 1920u, 1080u);
            }

            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    std::cout << (int)byte[i * 16 + j] << " ";
                }
                std::cout << "\n";
            }

            // The mesh drew into four colour targets and this is the first one,
            // read back. A cleared target reads as all zeros; anything the
            // pipeline wrote makes at least one channel non-zero. Without this
            // check the test passes on a frame that drew nothing at all, which is
            // exactly what a zero colour write mask produces.
            for (size_t i = 0; i < 16 * 16 * 4; i++) {
                if (std::to_integer<uint8_t>(byte[i]) != 0) {
                    g_frame_has_colour = true;
                    break;
                }
            }
        });

        const auto register_elapsed = sch::steady_clock::now() - register_start;
        if (register_elapsed > sch::milliseconds(100)) {
            std::cerr << "MRT test FAILED: registering a readback blocked for "
                      << sch::duration_cast<sch::milliseconds>(register_elapsed).count()
                      << " ms. Registration queues a callback; it must not wait for the device." << std::endl;
            return 1;
        }

        rsys->GetFrameManager().BeginMainCommandBuffer();
        rg->RecordIntoMainCommandBuffer(*rsys);

        rsys->CompleteFrame(
            *colors[color],
            color == 0 ? Rhi::MemoryAccessTypeImageBits::TransferRead
                       : Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite
        );

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    if (!g_frame_has_colour) {
        std::cerr << "MRT test FAILED: the read-back colour target is uniformly zero, "
                     "so the mesh drew nothing. A pipeline whose colour write mask is "
                     "zero produces exactly this."
                  << std::endl;
        return 1;
    }

    std::cout << "MRT test PASSED: the read-back colour target contains drawn pixels." << std::endl;
    return 0;
}







