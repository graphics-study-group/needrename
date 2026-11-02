#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"

#include <stdexcept>

namespace Engine {
    // The shape that has no render graph.
    //
    // The builder compiles a frame into pass order and, per pass, into Vulkan transitions. The
    // ordering half is portable; the transition half is 47 translations that would emit nothing
    // here. Until that split is done - the barrier work the change records as abandoned - this
    // translation unit stands in so the engine links, and each method says plainly that a graph
    // cannot be built.
    //
    // A caller that reaches one of these asked for a rendered frame on a backend that cannot
    // produce one yet. Saying so beats returning an empty graph that draws nothing.

    struct RenderGraphBuilder::impl {};

    RenderGraphBuilder::RenderGraphBuilder(RenderSystem &system) : system(system), pimpl(std::make_unique<impl>()) {
    }

    RenderGraphBuilder::~RenderGraphBuilder() = default;

    RGTextureHandle RenderGraphBuilder::ImportExternalResource(
        RenderTargetTexture &, Rhi::MemoryAccessTypeImageBits
    ) {
        throw std::logic_error(
            "RenderGraphBuilder: this backend has no render graph. Building one needs the barrier "
            "split the change records as unfinished."
        );
    }

    RGTextureHandle RenderGraphBuilder::ImportExternalResource(RRTTHandle, Rhi::MemoryAccessTypeImageBits) {
        throw std::logic_error("RenderGraphBuilder: this backend has no render graph.");
    }

    RGBufferHandle RenderGraphBuilder::ImportExternalResource(
        const Rhi::DeviceBuffer &, Rhi::MemoryAccessTypeBuffer
    ) {
        throw std::logic_error("RenderGraphBuilder: this backend has no render graph.");
    }

    RGTextureHandle RenderGraphBuilder::RequestRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc, RenderTargetTexture::SamplerDesc, std::string_view
    ) noexcept {
        // Marked noexcept by the interface, so this cannot throw. A handle that names nothing is
        // the only report available; a caller that uses it will fail at the first graph call.
        return RGTextureHandle{};
    }

    RGTextureHandle RenderGraphBuilder::RequestResizableRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc,
        RenderTargetTexture::SamplerDesc,
        float,
        float,
        std::string_view
    ) {
        throw std::logic_error("RenderGraphBuilder: this backend has no render graph.");
    }

    void RenderGraphBuilder::AddPass(RenderGraphPass &&) noexcept {
        // Nothing to add to.
    }

    std::unique_ptr<RenderGraph> RenderGraphBuilder::BuildRenderGraph() {
        throw std::logic_error("RenderGraphBuilder: this backend has no render graph.");
    }
} // namespace Engine

