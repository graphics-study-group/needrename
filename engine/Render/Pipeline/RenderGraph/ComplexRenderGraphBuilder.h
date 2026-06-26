#ifndef RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
#define RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H

#include "Asset/AssetRef.h"
#include "RGAttachmentDesc.h"

#include <memory>

namespace Engine {
    class ComputeBuffer;
    class ComputeStage;
    class RenderGraph;
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

        /**
         * @brief Build the default rendering render graph.
         *
         * @param texture_width             Viewport width.
         * @param texture_height            Viewport height.
         * @param final_color_target_id     Output handle for the final (post-bloom) color target.
         * @param model_matrices_buffer     Optional physics-owned model matrices buffer.
         *                                  When non-null, shadow map and lit passes read
         *                                  model matrices from this buffer.
         * @return Compiled RenderGraph.
         */
        std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(
            uint32_t texture_width,
            uint32_t texture_height,
            RGTextureHandle &final_color_target_id,
            const ComputeBuffer *model_matrices_buffer = nullptr
        );

    protected:
        RenderSystem &m_system;
        AssetRef m_bloom_shader{};
        std::shared_ptr<ComputeStage> m_bloom_compute_stage{};
    };
} // namespace Engine

#endif // RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
