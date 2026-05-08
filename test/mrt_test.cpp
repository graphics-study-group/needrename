#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>

#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Asset/Mesh/PlaneMeshAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Core/Functional/SDLWindow.h"
#include "Framework/component/RenderComponent/StaticMeshComponent.h"
#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"
#include "UserInterface/GUISystem.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>

#include "cmake_config.h"
#include <iostream>

using namespace Engine;
namespace sch = std::chrono;

constexpr glm::mat4 EYE4 = glm::mat4(1.0f);

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
    auto vs_ref = adb->GetNewAssetRef({*adb, "~/shaders/debug_writethrough.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef({*adb, "~/shaders/debug_writethrough_mrt.frag.asset"});

    test_asset->name = "Writethrough";

    MaterialTemplateSinglePassProperties mtspp{};
    mtspp.attachments.color = {
        ImageUtils::ImageFormat::R8G8B8A8UNorm,
        ImageUtils::ImageFormat::R8G8B8A8UNorm,
        ImageUtils::ImageFormat::R8G8B8A8UNorm,
        ImageUtils::ImageFormat::R8G8B8A8UNorm
    };
    using CBP = PipelineProperties::ColorBlendingProperties;
    CBP cbp;
    cbp.color_op = cbp.alpha_op = CBP::BlendOperation::None;
    mtspp.attachments.color_blending = {cbp, cbp, cbp, cbp};
    mtspp.attachments.depth = ImageUtils::ImageFormat::D32SFLOAT;
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
    DeviceBuffer *readback,
    RenderTargetTexture *color_1,
    RenderTargetTexture *color_2,
    RenderTargetTexture *color_3,
    RenderTargetTexture *color_4,
    RenderTargetTexture *depth,
    MaterialInstance *material,
    IVertexBasedRenderer *mesh
) {
    using IAT = Engine::MemoryAccessTypeImageBits;
    RenderGraphBuilder2 rgb{*rsys};
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
                 Engine::TextureSubresourceRange::GetSingleRange(),
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store}
            )
            .AppendColorAttachment(
                {c2,
                 Engine::TextureSubresourceRange::GetSingleRange(),
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store}
            )
            .AppendColorAttachment(
                {c3,
                 Engine::TextureSubresourceRange::GetSingleRange(),
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store}
            )
            .AppendColorAttachment(
                {c4,
                 Engine::TextureSubresourceRange::GetSingleRange(),
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
            .SetRasterizerPassFunction([rsys, color_1, color_2, color_3, color_4, depth, material, mesh](
                                           GraphicsCommandBuffer &gcb, const RenderGraph2 &
                                       ) {
                auto extent = rsys->GetSwapchain().GetExtent();
                gcb.SetupViewport(extent.width, extent.height, {{0, 0}, extent});
                gcb.BindSceneResources(rsys->GetSceneDataManager());
                gcb.BindCameraResources(rsys->GetCameraManager());

                PipelineRuntimeInfo pri{};
                pri.va.SetAttribute(VertexAttributeSemantic::Position, VertexAttributeType::SFloat32x3);
                pri.va.SetAttribute(VertexAttributeSemantic::Color, VertexAttributeType::SFloat32x3);
                pri.va.SetAttribute(VertexAttributeSemantic::Normal, VertexAttributeType::SFloat32x3);
                pri.va.SetAttribute(VertexAttributeSemantic::Texcoord0, VertexAttributeType::SFloat32x2);
                pri.color_attachment_format[0] = color_1->GetTextureDescription().format;
                pri.color_attachment_format[1] = color_2->GetTextureDescription().format;
                pri.color_attachment_format[2] = color_3->GetTextureDescription().format;
                pri.color_attachment_format[3] = color_4->GetTextureDescription().format;
                pri.color_attachment_format[4] = ImageUtils::ImageFormat::UNDEFINED;
                pri.depth_stencil_attachment_format = depth->GetTextureDescription().format;

                auto tpl = material->GetLibrary().FindMaterialTemplate("", pri);
                assert(tpl);
                gcb.BindMaterial(*material, *tpl);
                // Push model matrix...
                vk::CommandBuffer rcb = gcb.GetCommandBuffer();
                rcb.pushConstants(
                    material->GetLibrary().FindMaterialTemplate("", pri)->GetPipelineLayout(),
                    vk::ShaderStageFlagBits::eAllGraphics,
                    0,
                    sizeof(RenderSystemState::RendererManager::RendererDataStruct),
                    reinterpret_cast<const void *>(&EYE4)
                );
                gcb.DrawMesh(*mesh);
            })
            .WrapRenderPass()
            .Get()
    );

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Transfer")
            .UseBuffer(b1, {MemoryAccessTypeBufferBits::TransferWrite})
            .UseImage(c1, MemoryAccessTypeImageBits::TransferRead)
            .SetTransferPassFunction([readback, c1](TransferCommandBuffer &tcb, const RenderGraph2 &rg) {
                auto rt = rg.GetInternalTextureResource(c1);
                std::array image_copies = {vk::BufferImageCopy{
                    0,
                    0,
                    0,
                    vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                    vk::Offset3D{0, 0, 0},
                    vk::Extent3D{
                        rt->GetTextureDescription().width,
                        rt->GetTextureDescription().height,
                        rt->GetTextureDescription().depth
                    }
                }};
                tcb.GetCommandBuffer().copyImageToBuffer(
                    rt->GetImage(), vk::ImageLayout::eTransferSrcOptimal, readback->GetBuffer(), image_copies
                );
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
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();
    auto amg = cmc->GetAssetManager();

    // Prepare material
    auto [test_library_asset, test_template_asset] = ConstructMaterial();
    auto &ml_mng = rsys->GetRenderResourceManager<RenderSystemState::MaterialLibraryManager>();

    auto test_library_handle = ml_mng.CreateOrReuseFromAsset(test_library_asset->GetGUID());
    auto test_library = ml_mng.Resolve(test_library_handle);
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
    auto depth = RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Depth Attachment");
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm;
    std::array colors{
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Position)"),
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Vertex color)"),
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Normal)"),
        RenderTargetTexture::CreateUnique(*rsys, desc, Texture::SamplerDesc{}, "Color Attachment (Texcoord)")
    };
    auto readback_buffer = DeviceBuffer::CreateUnique(
        rsys->GetAllocatorState(),
        Engine::BufferType{Engine::BufferTypeBits::CopyTo, Engine::BufferTypeBits::CopyFrom},
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

        rsys->GetFrameManager().RegisterReadbackCallback(*readback_buffer, [](std::unique_ptr<DeviceBuffer> in) {
            auto byte = in->GetVMAddress();

            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    std::cout << (int)byte[i * 16 + j] << " ";
                }
                std::cout << "\n";
            }
        });

        rg.Execute(*rsys);

        rsys->CompleteFrame(
            *colors[color],
            color == 0 ? MemoryAccessTypeImageBits::TransferRead : MemoryAccessTypeImageBits::ColorAttachmentWrite,
            colors[color]->GetTextureDescription().width,
            colors[color]->GetTextureDescription().height
        );

        SDL_Delay(10);

        if (quited) break;
    }

    rsys->WaitForIdle();

    return 0;
}
