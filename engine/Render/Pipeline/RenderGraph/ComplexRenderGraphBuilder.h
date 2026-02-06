#ifndef RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
#define RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H

#include "RenderGraphBuilder.h"

namespace Engine {
    class AssetRef;
    class ComputeStage;

    class ComplexRenderGraphBuilder : public RenderGraphBuilder {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        ComplexRenderGraphBuilder(RenderSystem &system);
        ~ComplexRenderGraphBuilder() = default;

        std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(
            uint32_t texture_width,
            uint32_t texture_height,
            std::function<vk::Extent2D()> get_viewport_func,
            std::function<uint8_t()> get_camera_index_func,
            int32_t &final_color_target_id
        );

    protected:
        std::shared_ptr<AssetRef> m_bloom_shader{};
        std::shared_ptr<ComputeStage> m_bloom_compute_stage{};
    };
} // namespace Engine

#endif // RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
