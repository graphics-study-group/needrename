#include "ShadowCascadeResources.h"

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
        auto &scene_data = system.GetSceneDataManager();

        // The storage exists before this graph is built and its objects are never
        // replaced, so the graph imports every budgeted slot unconditionally. That
        // is what lets a light appear or disappear later without the graph being
        // rebuilt: the slots do not depend on the scene at all.
        std::vector<RGTextureHandle> shadow_ids;
        shadow_ids.reserve(SceneDataManager::MAX_SHADOW_CASTING_LIGHTS);
        for (uint32_t slot = 0; slot < SceneDataManager::MAX_SHADOW_CASTING_LIGHTS; slot++) {
            shadow_ids.push_back(builder.ImportExternalResource(scene_data.GetShadowMapTexture(slot)));
        }

        const RendererManager::FilterCriteria shadow_caster_filter{
            RendererManager::FilterCriteria::BinaryCriterion::Yes
        };

        /**
         * Shadowmap passes — one per budgeted slot, each rendering all of its
         * light's cascades into their own array layer.
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
                    .SetPassFunction([&system, light, shadow_caster_filter, shadow_id = shadow_ids[light]](
                                         CommandBuffer &cb, const RenderGraph &rg
                                     ) {
                        const auto &scene_data = system.GetSceneDataManager();
                        auto *shadow_map_target = rg.GetInternalTextureResource(shadow_id);
                        const vk::Extent2D shadow_map_extent{
                            SceneDataManager::SHADOWMAP_SIZE, SceneDataManager::SHADOWMAP_SIZE
                        };
                        vk::Rect2D shadow_map_scissor{{0, 0}, shadow_map_extent};

                        if (light >= scene_data.GetNumShadowCastingLights()) return;
                        // A light is still missing its cascades on the very first
                        // frame, or when its configuration could not be fitted.
                        const uint32_t cascade_count = std::min(
                            scene_data.GetLightCascadeCount(static_cast<uint32_t>(light)),
                            SceneDataManager::MAX_SHADOW_CASCADES
                        );
                        if (cascade_count == 0) {
                            // Nothing is drawn into the slot this frame, so the whole
                            // array texture is cleared: the slot must never keep
                            // contents from an earlier frame, whichever cascade the
                            // shading ends up reading.
                            cb.BeginRendering(
                                {nullptr},
                                {shadow_map_target,
                                 {},
                                 AttachmentUtils::LoadOperation::Clear,
                                 AttachmentUtils::StoreOperation::Store,
                                 AttachmentUtils::DepthClearValue{1.0f, 0U}},
                                shadow_map_extent,
                                "Shadowmap Pass"
                            );
                            cb.EndRendering();
                            return;
                        }

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
                    })
                    .Get()
                // NOTE: No WrapRenderPass() — the pass manages its cascade
                // renderings with explicit BeginRendering/EndRendering calls.
            );
        }

        return shadow_ids;
    }
} // namespace Engine
