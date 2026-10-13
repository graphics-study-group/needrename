#include "ShadowCascadeResources.h"

#include <Framework/MainClass.h>
#include <Framework/World/WorldSystem.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/RendererManager.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/Resource/RenderTargetTexture.h>

#include <algorithm>
#include <string>
#include <vulkan/vulkan.hpp>

namespace Engine {
    using RenderSystemState::RendererManager;
    using RenderSystemState::SceneDataManager;

    std::vector<RGTextureHandle> AddShadowCascadePasses(
        RenderGraphBuilder &builder, RenderSystem &system, RGBufferHandle model_matrices
    ) {
        // Only the lights that exist while the graph is being built can own
        // storage: the graph owns its transient textures and is built once.
        uint32_t light_count = 0;
        if (auto world_system = MainClass::GetInstance()->GetWorldSystem()) {
            light_count = world_system->GetShadowCastingLightCount();
        }

        RenderTargetTexture::RenderTargetTextureDesc shadow_desc{
            .dimensions = 2,
            .width = SceneDataManager::SHADOWMAP_SIZE,
            .height = SceneDataManager::SHADOWMAP_SIZE,
            .depth = 1,
            .mipmap_levels = 1,
            .array_layers = SceneDataManager::MAX_SHADOW_CASCADES,
            .format = RenderTargetTexture::RTTFormat::D32SFLOAT,
            .multisample = 1,
            .is_cube_map = false
        };
        std::vector<RGTextureHandle> shadow_ids;
        shadow_ids.reserve(light_count);
        for (uint32_t i = 0; i < light_count; i++) {
            shadow_ids.push_back(builder.RequestRenderTargetTexture(
                shadow_desc, Rhi::Texture::SamplerDesc{}, "Shadow Map " + std::to_string(i)
            ));
        }

        const RendererManager::FilterCriteria shadow_caster_filter{
            RendererManager::FilterCriteria::BinaryCriterion::Yes
        };

        /**
         * Shadowmap passes — one per shadow-casting light, each rendering all of
         * that light's cascades into their own array layer.
         */
        for (size_t light = 0; light < shadow_ids.size(); light++) {
            auto pass_builder = RenderGraphPassBuilder{system}.SetName("Shadowmap Pass " + std::to_string(light));
            pass_builder.UseBuffer(
                model_matrices, Rhi::MemoryAccessTypeBuffer(Rhi::MemoryAccessTypeBufferBits::ShaderRandomRead)
            );
            builder.AddPass(
                pass_builder
                    .SetDepthStencilAttachment(
                        {shadow_ids[light],
                         {},
                         AttachmentUtils::LoadOperation::Clear,
                         AttachmentUtils::StoreOperation::Store,
                         AttachmentUtils::DepthClearValue{1.0f, 0U}}
                    )
                    .SetPassFunction(
                        [&system, shadow_ids, light, shadow_caster_filter](CommandBuffer &cb, const RenderGraph &rg) {
                            const auto &scene_data = system.GetSceneDataManager();
                            if (light >= scene_data.GetNumShadowCastingLights()) return;
                            // A light is still missing its cascades on the very first
                            // frame, or when its configuration could not be fitted.
                            const uint32_t cascade_count = std::min(
                                scene_data.GetLightCascadeCount(static_cast<uint32_t>(light)),
                                SceneDataManager::MAX_SHADOW_CASCADES
                            );
                            if (cascade_count == 0) return;

                            const vk::Extent2D shadow_map_extent{
                                SceneDataManager::SHADOWMAP_SIZE, SceneDataManager::SHADOWMAP_SIZE
                            };
                            vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};
                            auto *shadow_map_target = rg.GetInternalTextureResource(shadow_ids[light]);
                            const RendererList renderers =
                                system.GetRendererManager().FilterAndSortRenderers(shadow_caster_filter);

                            for (uint32_t cascade = 0; cascade < cascade_count; cascade++) {
                                cb.BeginRendering(
                                    {nullptr},
                                    {shadow_map_target,
                                     Rhi::TextureSubresourceRange{cascade, 1, 0, 1},
                                     AttachmentUtils::LoadOperation::Clear,
                                     AttachmentUtils::StoreOperation::Store,
                                     AttachmentUtils::DepthClearValue{1.0f, 0U}},
                                    shadow_map_extent,
                                    "Shadowmap Pass"
                                );
                                cb.SetupViewport(shadow_map_extent.width, shadow_map_extent.height, shadow_map_scissor);
                                cb.DrawRenderers(
                                    "Shadowmap",
                                    renderers,
                                    static_cast<int32_t>(light * SceneDataManager::MAX_SHADOW_CASCADES + cascade),
                                    shadow_map_extent
                                );
                                cb.EndRendering();
                            }
                        }
                    )
                    .Get()
                // NOTE: No WrapRenderPass() — the pass manages its cascade
                // renderings with explicit BeginRendering/EndRendering calls.
            );
        }

        return shadow_ids;
    }

    void RegisterShadowCascadeMaps(
        RenderGraph &render_graph, RenderSystem &system, const std::vector<RGTextureHandle> &shadow_map_ids
    ) {
        auto &scene_data = system.GetSceneDataManager();
        // Clear first: the graph that owned the previously registered maps may
        // already be destroyed, and a stale pointer must never be sampled.
        for (uint32_t i = 0; i < SceneDataManager::MAX_SHADOW_CASTING_LIGHTS; i++) {
            scene_data.RemoveLightShadowMap(i);
        }
        for (size_t light = 0; light < shadow_map_ids.size(); light++) {
            scene_data.SetLightShadowMap(
                static_cast<uint32_t>(light), *render_graph.GetInternalTextureResource(shadow_map_ids[light])
            );
        }
    }
} // namespace Engine
