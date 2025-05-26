#include "GraphicsContext.h"

#include <queue>
#include <SDL3/SDL.h>
#include "Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h"
#include "ComputeContext.h"

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
    void GraphicsContext::UseImage(vk::Image img, ImageAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
        vk::ImageMemoryBarrier2 barrier;
        barrier.image = img;
        barrier.subresourceRange = vk::ImageSubresourceRange{
            vk::ImageAspectFlagBits::eColor, 
            0, vk::RemainingMipLevels, 
            0, vk::RemainingArrayLayers
        };

        // TODO: Infer aspect mask from image format, instead of usage.
        switch(previousAccess) {
            case ImageAccessType::ColorAttachmentRead:
            case ImageAccessType::ColorAttachmentWrite:
            case ImageAccessType::ShaderRead:
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                break;
            case ImageAccessType::DepthAttachmentRead:
            case ImageAccessType::DepthAttachmentWrite:
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
                break;
            default:
                switch(currentAccess) {
                    case ImageAccessType::ColorAttachmentRead:
                    case ImageAccessType::ColorAttachmentWrite:
                    case ImageAccessType::ShaderRead:
                        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                        break;
                    case ImageAccessType::DepthAttachmentRead:
                    case ImageAccessType::DepthAttachmentWrite:
                        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
                        break;
                    default:
                        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eNone;
                }
        }

        if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eNone) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to infer aspect range when inserting an image barrier.");
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        }

        std::tie(barrier.dstStageMask, barrier.dstAccessMask, barrier.newLayout) = AccessHelper::GetAccessScope(currentAccess);
        std::tie(barrier.srcStageMask, barrier.srcAccessMask, barrier.oldLayout) = AccessHelper::GetAccessScope(previousAccess);
        barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;

        pimpl->barriers.push_back(std::move(barrier));
    }

    void GraphicsContext::UseImage(vk::Image img, ImageGraphicsAccessType currentAccess, ImageAccessType previousAccess) noexcept
    {
        using type = ImageGraphicsAccessType;
        switch(currentAccess) {
            case type::ColorAttachmentRead:
                this->UseImage(img, ImageAccessType::ColorAttachmentRead, previousAccess);
                break;
            case type::DepthAttachmentRead:
                this->UseImage(img, ImageAccessType::DepthAttachmentRead, previousAccess);
                break;
            case type::ShaderRead:
                this->UseImage(img, ImageAccessType::ShaderRead, previousAccess);
                break;
            case type::ColorAttachmentWrite:
                this->UseImage(img, ImageAccessType::ColorAttachmentWrite, previousAccess);
                break;
            case type::DepthAttachmentWrite:
                this->UseImage(img, ImageAccessType::DepthAttachmentWrite, previousAccess);
                break;
        }
    }
    void GraphicsContext::PrepareCommandBuffer()
    {
        if (pimpl->barriers.empty())    return;
        vk::DependencyInfo dep{vk::DependencyFlags{0}, {}, {}, pimpl->barriers};
        this->GetCommandBuffer().GetCommandBuffer().pipelineBarrier2(dep);
    }
}
