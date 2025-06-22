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

    GraphicsContext::GraphicsContext(
        RenderSystem & system,
        vk::CommandBuffer cb,
        uint32_t frame_in_flight
    ) : TransferContext(system, cb, frame_in_flight), pimpl(std::make_unique<GraphicsContext::impl>(GraphicsCommandBuffer(system, cb, frame_in_flight)))
    {
    }

    GraphicsContext::~GraphicsContext() = default;

    ICommandBuffer &GraphicsContext::GetCommandBuffer() const noexcept
    {
        return pimpl->cb;
    }
    void GraphicsContext::UseImage(const Texture & texture, ImageGraphicsAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
        vk::ImageMemoryBarrier2 barrier;
        using type = ImageGraphicsAccessType;
        switch(currentAccess) {
            case type::ColorAttachmentRead:
                barrier = GetImageBarrier(texture, ImageAccessType::ColorAttachmentRead, previousAccess);
                break;
            case type::DepthAttachmentRead:
                barrier = GetImageBarrier(texture, ImageAccessType::DepthAttachmentRead, previousAccess);
                break;
            case type::ShaderRead:
                barrier = GetImageBarrier(texture, ImageAccessType::ShaderRead, previousAccess);
                break;
            case type::ColorAttachmentWrite:
                barrier = GetImageBarrier(texture, ImageAccessType::ColorAttachmentWrite, previousAccess);
                break;
            case type::DepthAttachmentWrite:
                barrier = GetImageBarrier(texture, ImageAccessType::DepthAttachmentWrite, previousAccess);
                break;
        }
        pimpl->barriers.push_back(std::move(barrier));
    }
    void GraphicsContext::PrepareCommandBuffer()
    {
        TransferContext::PrepareCommandBuffer();

        if (pimpl->barriers.empty())    return;
        vk::DependencyInfo dep{vk::DependencyFlags{0}, {}, {}, pimpl->barriers};
        this->GetCommandBuffer().GetCommandBuffer().pipelineBarrier2(dep);
        pimpl->barriers.clear();
    }
}
