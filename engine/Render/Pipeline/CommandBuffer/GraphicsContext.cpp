#include "GraphicsContext.h"

#include <queue>
#include <SDL3/SDL.h>
#include "Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h"
#include "ComputeContext.h"

#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

namespace Engine {
    struct GraphicsContext::impl {
        GraphicsCommandBuffer cb;
        std::vector <vk::ImageMemoryBarrier2> barriers {};
        impl(GraphicsCommandBuffer && _cb) : cb(std::move(_cb)) {};
    };

    GraphicsContext::GraphicsContext(GraphicsCommandBuffer &&cb) : pimpl(std::make_unique<GraphicsContext::impl>(std::move(cb)))
    {
    }

    GraphicsContext::~GraphicsContext() = default;

    ICommandBuffer &GraphicsContext::GetCommandBuffer() const noexcept
    {
        return pimpl->cb;
    }
    void GraphicsContext::UseImage(vk::Image img, ImageGraphicsAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
        vk::ImageMemoryBarrier2 barrier;
        using type = ImageGraphicsAccessType;
        switch(currentAccess) {
            case type::ColorAttachmentRead:
                barrier = GetImageBarrier(img, ImageAccessType::ColorAttachmentRead, previousAccess);
                break;
            case type::DepthAttachmentRead:
                barrier = GetImageBarrier(img, ImageAccessType::DepthAttachmentRead, previousAccess);
                break;
            case type::ShaderRead:
                barrier = GetImageBarrier(img, ImageAccessType::ShaderRead, previousAccess);
                break;
            case type::ColorAttachmentWrite:
                barrier = GetImageBarrier(img, ImageAccessType::ColorAttachmentWrite, previousAccess);
                break;
            case type::DepthAttachmentWrite:
                barrier = GetImageBarrier(img, ImageAccessType::DepthAttachmentWrite, previousAccess);
                break;
        }
        pimpl->barriers.push_back(std::move(barrier));
    }
    void GraphicsContext::PrepareCommandBuffer()
    {
        if (pimpl->barriers.empty())    return;
        vk::DependencyInfo dep{vk::DependencyFlags{0}, {}, {}, pimpl->barriers};
        this->GetCommandBuffer().GetCommandBuffer().pipelineBarrier2(dep);
    }
}
