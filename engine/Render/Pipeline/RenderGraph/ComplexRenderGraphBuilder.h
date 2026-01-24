#ifndef RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
#define RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H

#include "RenderGraphBuilder.h"

namespace Engine {
    class RenderTargetTexture;

    class ComplexRenderGraphBuilder : public RenderGraphBuilder {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        ComplexRenderGraphBuilder(RenderSystem &system);
        ~ComplexRenderGraphBuilder() = default;

        RenderGraph BuildDefaultRenderGraph(RenderTargetTexture &color_target, RenderTargetTexture &depth_target);

    protected:
        std::shared_ptr<RenderTargetTexture> m_shadow_target{};
    };
} // namespace Engine

#endif // RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
