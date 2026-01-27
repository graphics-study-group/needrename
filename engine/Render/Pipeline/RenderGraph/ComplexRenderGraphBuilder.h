#ifndef RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
#define RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H

#include "RenderGraphBuilder.h"

namespace Engine {
    class AssetRef;
    class RenderTargetTexture;

    class ComplexRenderGraphBuilder : public RenderGraphBuilder {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        ComplexRenderGraphBuilder(RenderSystem &system);
        ~ComplexRenderGraphBuilder() = default;

        std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(
            std::shared_ptr<const RenderTargetTexture> color_target_ptr,
            std::shared_ptr<const RenderTargetTexture> depth_target_ptr
        );
        
        MemoryAccessTypeImageBits GetColorAttachmentAccessType() const;

    protected:
        std::shared_ptr<RenderTargetTexture> m_shadow_target{};
        std::shared_ptr<RenderTargetTexture> m_hdr_color_target{};
        std::shared_ptr<RenderTargetTexture> m_bloom_temp{};

        std::shared_ptr<AssetRef> m_bloom_shader{};
    };
} // namespace Engine

#endif // RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
