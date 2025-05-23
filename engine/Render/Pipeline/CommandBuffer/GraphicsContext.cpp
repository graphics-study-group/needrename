#include "GraphicsContext.h"

#include "Render/Pipeline/CommandBuffer/RenderCommandBuffer.h"

namespace Engine {
    struct GraphicsContext::impl {
        RenderCommandBuffer cb;

        impl(RenderCommandBuffer && _cb) : cb(std::move(_cb)) {};
    };

    GraphicsContext::GraphicsContext(RenderCommandBuffer &&cb) : pimpl(std::make_unique<GraphicsContext::impl>(std::move(cb)))
    {
    }

    GraphicsContext::~GraphicsContext() = default;

    ICommandBuffer &GraphicsContext::GetCommandBuffer() const noexcept
    {
        return pimpl->cb;
    }
    void GraphicsContext::UseImage(vk::Image &img, AccessType currentAccess, AccessType previousAccess, ContextType previousCtx) noexcept
    {
    }
    void GraphicsContext::PrepareCommandBuffer()
    {
    }
}
