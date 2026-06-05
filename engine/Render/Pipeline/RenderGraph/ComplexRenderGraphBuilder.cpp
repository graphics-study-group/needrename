#include "ComplexRenderGraphBuilder.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/Shader/ShaderAsset.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Render/Memory/RenderTargetTexture.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/RenderGraph2/RenderGraph2.h>
#include <Render/Pipeline/RenderGraph2/RenderGraphBuilder2.h>
#include <Render/Pipeline/RenderGraph2/RenderGraphPass.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/Renderer/Camera.h>

#include <vulkan/vulkan.hpp>

namespace Engine {
    ComplexRenderGraphBuilder::ComplexRenderGraphBuilder(RenderSystem &system) : m_system(system) {
        // XXX: Hardcoded bloom shader. Should use AssetManager to load shader when we have pipeline asset.
        auto &adb = *std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
        m_bloom_shader = adb.GetNewAssetRef(AssetPath{adb, "~/shaders/bloom.comp.asset"});
    }

    std::unique_ptr<RenderGraph2> ComplexRenderGraphBuilder::BuildDefaultRenderGraph(
        uint32_t texture_width, uint32_t texture_height, RGTextureHandle &final_color_target_id
    ) {
        RenderGraphBuilder2 rgb{m_system};

        // Request transient resources
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

        // Set up bloom compute stage
        m_bloom_compute_stage = std::make_shared<ComputeStage>(m_system);
        m_bloom_compute_stage->Instantiate(*m_bloom_shader.as<ShaderAsset>());

        auto &system = m_system;
        auto world_system = MainClass::GetInstance()->GetWorldSystem().get();
        auto &bloom_compute_stage = *m_bloom_compute_stage;
        using IAT = MemoryAccessTypeImageBits;

        /**
         * Shadowmap passes — one per shadow-casting light.
         * Manual BeginRendering/EndRendering, no WrapRenderPass().
         */
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
                    .SetRasterizerPassFunction([&system,
                                                shadow_ids,
                                                i](GraphicsCommandBuffer &gcb, const RenderGraph2 &rg) {
                        vk::Extent2D shadow_map_extent{SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT};
                        vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
                        if (i < system.GetSceneDataManager().GetNumShadowCastingLights()) {
                            auto shadow_map_target = rg.GetInternalTextureResource(shadow_ids[i]);
                            gcb.BeginRendering(
                                {nullptr},
                                {shadow_map_target,
                                 TextureSubresourceRange::GetSingleRange(),
                                 AttachmentUtils::LoadOperation::Clear,
                                 AttachmentUtils::StoreOperation::Store,
                                 AttachmentUtils::DepthClearValue{1.0f, 0U}},
                                shadow_map_extent,
                                "Shadowmap Pass"
                            );
                            gcb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
                            gcb.DrawRenderers(
                                "Shadowmap",
                                system.GetRendererManager().FilterAndSortRenderers({}),
                                0,
                                shadow_map_extent
                            );
                            gcb.EndRendering();
                        }
                    })
                    .Get()
                // NOTE: No WrapRenderPass() — we manually manage BeginRendering/EndRendering
            );
        }

        /**
         * Main Lit pass — rasterizer with color + depth attachments, WrapRenderPass().
         */
        {
            auto lit_pass_builder = RenderGraphPassBuilder{m_system}.SetName("Main Lit pass");

            // Declare shadow map reads
            for (size_t i = 0; i < shadow_ids.size(); i++) {
                lit_pass_builder.UseImage(shadow_ids[i], IAT::ShaderSampledRead);
            }

            lit_pass_builder
                .AppendColorAttachment(
                    {hdr_color_id, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
                )
                .SetDepthStencilAttachment(
                    {depth_id,
                     {},
                     AttachmentUtils::LoadOperation::Clear,
                     AttachmentUtils::StoreOperation::DontCare,
                     AttachmentUtils::DepthClearValue{1.0f, 0U}}
                )
                .SetRasterizerPassFunction([&system, world_system](GraphicsCommandBuffer &gcb, const RenderGraph2 &) {
                    vk::Extent2D extent{system.GetSwapchain().GetExtent()};
                    vk::Rect2D scissor{{0, 0}, extent};
                    gcb.SetupViewport(extent.width, extent.height, scissor);
                    auto active_camera = world_system->GetActiveCamera();
                    if (active_camera == nullptr) {
                        // No active camera, skip rendering.
                        return;
                    }
                    system.GetCameraManager().SetActiveCameraIndex(active_camera->m_display_id);
                    gcb.DrawRenderers(
                        "Lit",
                        system.GetRendererManager().FilterAndSortRenderers({}),
                        system.GetCameraManager().GetActiveCameraIndex(),
                        extent
                    );
                    system.GetSceneDataManager().DrawSkybox(
                        gcb,
                        system.GetFrameManager().GetFrameInFlight(),
                        system.GetCameraManager().GetPVMatForSkybox(),
                        extent
                    );
                })
                .WrapRenderPass();
            rgb.AddPass(lit_pass_builder.Get());
        }

        /**
         * Bloom FX compute pass.
         */
        auto &bloom_compute_binding = bloom_compute_stage.AllocateResourceBinding();
        rgb.AddPass(
            RenderGraphPassBuilder{m_system}
                .SetName("Bloom FX pass")
                .UseImage(hdr_color_id, IAT::ShaderRandomRead)
                .UseImage(final_color_target_id, IAT::ShaderRandomWrite)
                .SetComputePassFunction([&bloom_compute_stage,
                                         texture_width,
                                         texture_height,
                                         &bloom_compute_binding,
                                         hdr_color_id,
                                         final_color_target_id](ComputeCommandBuffer &ccb, const RenderGraph2 &rg) {
                    bloom_compute_binding.GetShaderResourceBinding().BindTexture(
                        "inputImage", *rg.GetInternalTextureResource(hdr_color_id)
                    );
                    bloom_compute_binding.GetShaderResourceBinding().BindTexture(
                        "outputImage", *rg.GetInternalTextureResource(final_color_target_id)
                    );
                    ccb.BindComputeStage(bloom_compute_stage);
                    ccb.BindComputeResource(bloom_compute_binding);
                    ccb.DispatchCompute(texture_width / 16 + 1, texture_height / 16 + 1, 1);
                })
                .Get()
        );

        auto rg = rgb.BuildRenderGraph();

        // Post-build: register shadow maps with scene data manager
        for (size_t i = 0; i < shadow_ids.size(); i++) {
            auto shadow_map_target = rg->GetInternalTextureResource(shadow_ids[i]);
            system.GetSceneDataManager().SetLightShadowMap(i, *shadow_map_target);
        }

        return rg;
    }
} // namespace Engine
