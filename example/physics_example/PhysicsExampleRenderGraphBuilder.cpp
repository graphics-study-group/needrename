#include "PhysicsExampleRenderGraphBuilder.h"

#include <vulkan/vulkan.hpp>

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/Shader/ShaderAsset.h"
#include "Framework/world/WorldSystem.h"
#include "MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/XPBDGpuSolver.h"
#include "Render/Memory/ComputeBuffer.h"
#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Memory/RenderTargetTexture.h"
#include "Render/Memory/ShaderParameters/ShaderResourceBinding.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/Compute/ComputeResourceBinding.h"
#include "Render/Pipeline/Compute/ComputeStage.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SceneDataManager.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Renderer/Camera.h"

using namespace Engine;

PhysicsExampleRenderGraphBuilder::PhysicsExampleRenderGraphBuilder(RenderSystem &system) :
    m_system(system), m_xpbd_solver(std::make_unique<XPBDGpuSolver>(system)) {
    // Load bloom shader (mirrors ComplexRenderGraphBuilder).
    auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    m_bloom_shader = adb.GetNewAssetRef(AssetPath{adb, "~/shaders/bloom.comp.asset"});
}

PhysicsExampleRenderGraphBuilder::~PhysicsExampleRenderGraphBuilder() = default;

std::unique_ptr<RenderGraph> PhysicsExampleRenderGraphBuilder::BuildRenderGraph(
    uint32_t texture_width, uint32_t texture_height, PhysicsScene &physics_scene, RGTextureHandle &final_color_target_id
) {
    RenderGraphBuilder rgb{m_system};

    // ---- Import model matrices buffer from physics scene ----
    // Imported once so the XPBD compute writes and the lit pass vertex read
    // share the same handle — the render graph inserts correct barriers.
    auto gpu = physics_scene.GetGpuBuffers();
    auto mm_handle =
        rgb.ImportExternalResource(*gpu.model_matrices, MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::None));

    // ---- XPBD physics compute passes ----
    m_xpbd_solver->Step(rgb, physics_scene, mm_handle);

    // ---- Request transient render targets ----
    RenderTargetTexture::RenderTargetTextureDesc rtt_desc{
        .dimensions = 2,
        .width = texture_width,
        .height = texture_height,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    auto color_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "Final Color");
    final_color_target_id = color_id;
    rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R11G11B10UFloat;
    auto hdr_color_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "HDR Color");
    rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    auto depth_id = rgb.RequestRenderTargetTexture(rtt_desc, Texture::SamplerDesc{}, "Depth");

    // Shadow map render targets (one per shadow-casting light).
    RenderTargetTexture::RenderTargetTextureDesc shadow_desc{
        .dimensions = 2,
        .width = SHADOWMAP_WIDTH,
        .height = SHADOWMAP_HEIGHT,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT,
        .multisample = 1,
        .is_cube_map = false
    };
    std::vector<RGTextureHandle> shadow_ids;
    shadow_ids.resize(RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS);
    for (size_t i = 0; i < shadow_ids.size(); i++) {
        shadow_ids[i] =
            rgb.RequestRenderTargetTexture(shadow_desc, Texture::SamplerDesc{}, "Shadow Map " + std::to_string(i));
    }

    // Set up bloom compute stage.
    m_bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
    m_bloom_compute_stage->Instantiate(*m_bloom_shader.as<ShaderAsset>());

    auto &system = m_system;
    auto world_system = MainClass::GetInstance()->GetWorldSystem().get();
    auto &bloom_compute_stage = *m_bloom_compute_stage;
    using IAT = MemoryAccessTypeImageBits;

    // ---- Shadow map passes (one per shadow-casting light) ----
    for (size_t i = 0; i < shadow_ids.size(); i++) {
        rgb.AddPass(
            RenderGraphPassBuilder{m_system}
                .SetName("Shadowmap Pass " + std::to_string(i))
                .SetDepthStencilAttachment(
                    {shadow_ids[i],
                     {},
                     AttachmentUtils::LoadOperation::Clear,
                     AttachmentUtils::StoreOperation::Store,
                     AttachmentUtils::DepthClearValue{1.0f, 0U}}
                )
                .SetPassFunction([&system, shadow_ids, i](CommandBuffer &cb, const RenderGraph &rg) {
                    vk::Extent2D shadow_map_extent{SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT};
                    vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
                    if (i < system.GetSceneDataManager().GetNumShadowCastingLights()) {
                        auto shadow_map_target = rg.GetInternalTextureResource(shadow_ids[i]);
                        cb.BeginRendering(
                            {nullptr},
                            {shadow_map_target,
                             TextureSubresourceRange::GetSingleRange(),
                             AttachmentUtils::LoadOperation::Clear,
                             AttachmentUtils::StoreOperation::Store,
                             AttachmentUtils::DepthClearValue{1.0f, 0U}},
                            shadow_map_extent,
                            "Shadowmap Pass"
                        );
                        cb.SetupViewport(
                            static_cast<float>(shadow_map_extent.width),
                            static_cast<float>(shadow_map_extent.height),
                            shadow_map_scissor
                        );
                        cb.DrawRenderers(
                            "Shadowmap", system.GetRendererManager().FilterAndSortRenderers({}), 0, shadow_map_extent
                        );
                        cb.EndRendering();
                    }
                })
                .Get()
        );
    }

    // ---- Main Lit pass ----
    {
        auto lit_pass_builder = RenderGraphPassBuilder{m_system}.SetName("Main Lit pass");

        // Declare shadow map reads.
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            lit_pass_builder.UseImage(shadow_ids[i], IAT::ShaderSampledRead);
        }

        // Declare model matrix buffer read so the render graph inserts the
        // compute-write → vertex-read barrier between XPBD and Lit passes.
        lit_pass_builder.UseBuffer(mm_handle, MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::ShaderRandomRead));

        lit_pass_builder
            .AppendColorAttachment(
                {hdr_color_id,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::Store,
                 AttachmentUtils::ColorClearValue{0.1f, 0.1f, 0.15f, 1.0f}}
            )
            .SetDepthStencilAttachment(
                {depth_id,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::DontCare,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}}
            )
            .SetPassFunction([&system, world_system](CommandBuffer &cb, const RenderGraph &) {
                vk::Extent2D extent = system.GetSwapchain().GetExtent();
                vk::Rect2D scissor{{0, 0}, extent};
                cb.SetupViewport(static_cast<float>(extent.width), static_cast<float>(extent.height), scissor);

                auto active_camera = world_system->GetActiveCamera();
                if (active_camera == nullptr) {
                    return;
                }
                system.GetCameraManager().SetActiveCameraIndex(active_camera->m_display_id);
                cb.DrawRenderers(
                    "Lit",
                    system.GetRendererManager().FilterAndSortRenderers({}),
                    system.GetCameraManager().GetActiveCameraIndex(),
                    extent
                );
                system.GetSceneDataManager().DrawSkybox(
                    cb,
                    system.GetFrameManager().GetFrameInFlight(),
                    system.GetCameraManager().GetPVMatForSkybox(),
                    extent
                );
            })
            .WrapRenderPass();
        rgb.AddPass(lit_pass_builder.Get());
    }

    // ---- Bloom FX compute pass ----
    auto &bloom_compute_binding = bloom_compute_stage.AllocateResourceBinding();
    rgb.AddPass(
        RenderGraphPassBuilder{m_system}
            .SetName("Bloom FX pass")
            .UseImage(hdr_color_id, IAT::ShaderRandomRead)
            .UseImage(final_color_target_id, IAT::ShaderRandomWrite)
            .SetAffinity(RenderGraphPassAffinity::Compute)
            .SetPassFunction([&bloom_compute_stage,
                              texture_width,
                              texture_height,
                              &bloom_compute_binding,
                              hdr_color_id,
                              final_color_target_id](CommandBuffer &cb, const RenderGraph &rg) {
                bloom_compute_binding.GetShaderResourceBinding().BindTexture(
                    "inputImage", *rg.GetInternalTextureResource(hdr_color_id)
                );
                bloom_compute_binding.GetShaderResourceBinding().BindTexture(
                    "outputImage", *rg.GetInternalTextureResource(final_color_target_id)
                );
                cb.BindComputeStage(bloom_compute_stage);
                cb.BindComputeResource(bloom_compute_binding);
                cb.DispatchCompute(texture_width / 16 + 1, texture_height / 16 + 1, 1);
            })
            .Get()
    );

    auto rg = rgb.BuildRenderGraph();

    // Post-build: register shadow maps with scene data manager.
    for (size_t i = 0; i < shadow_ids.size(); i++) {
        auto shadow_map_target = rg->GetInternalTextureResource(shadow_ids[i]);
        system.GetSceneDataManager().SetLightShadowMap(static_cast<uint32_t>(i), *shadow_map_target);
    }

    return rg;
}
