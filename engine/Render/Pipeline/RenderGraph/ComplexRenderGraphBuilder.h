#ifndef RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
#define RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H

#include "Asset/AssetRef.h"
#include "RGAttachmentDesc.h"

#include <memory>

namespace Engine {
    class ComputeStage;
    class RenderGraph2;
    class RenderSystem;

    /**
     * @brief A render graph that integrates all current rendering features (Shadow, PBR, Blinn-Phong, etc.)
     * TODO: Need better way to manage the render graph.
     */
    class ComplexRenderGraphBuilder {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        ComplexRenderGraphBuilder(RenderSystem &system);
        ~ComplexRenderGraphBuilder() = default;

        std::unique_ptr<RenderGraph2> BuildDefaultRenderGraph(
            uint32_t texture_width, uint32_t texture_height, RGTextureHandle &final_color_target_id
        );

    protected:
        RenderSystem &m_system;
        AssetRef m_bloom_shader{};
        std::shared_ptr<ComputeStage> m_bloom_compute_stage{};
    };
} // namespace Engine

#endif // RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
