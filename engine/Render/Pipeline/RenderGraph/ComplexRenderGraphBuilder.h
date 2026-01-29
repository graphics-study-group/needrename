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

        std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(uint32_t width, uint32_t height);

        MemoryAccessTypeImageBits GetColorAttachmentAccessType() const;
        uint32_t GetFinalColorAttachmentID() const;

    protected:
        std::shared_ptr<AssetRef> m_bloom_shader{};
        uint32_t m_final_color_attachment_id{0};
    };
} // namespace Engine

#endif // RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
