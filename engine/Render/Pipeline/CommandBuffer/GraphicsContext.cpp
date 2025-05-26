#include "GraphicsContext.h"

#include <queue>
#include <SDL3/SDL.h>
#include "Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h"

namespace Engine {
    struct GraphicsContext::impl {
        GraphicsCommandBuffer cb;

        struct GraphicsBarrierDescriptor {
            struct SyncScope{
                vk::PipelineStageFlags2 stage;
                vk::AccessFlags2 access;
                vk::ImageLayout layout;
                uint32_t queue;
            } src, dst;
            vk::Image image;
            vk::ImageSubresourceRange range;
        };
        std::deque <GraphicsBarrierDescriptor> barriers;

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
        impl::GraphicsBarrierDescriptor descriptor;
        descriptor.image = img;
        descriptor.range = vk::ImageSubresourceRange{
            vk::ImageAspectFlagBits::eColor, 
            0, vk::RemainingMipLevels, 
            0, vk::RemainingArrayLayers
        };

        // TODO: Infer aspect mask from image format, instead of usage.
        switch(previousAccess) {
            case ImageAccessType::ColorAttachmentRead:
            case ImageAccessType::ColorAttachmentWrite:
            case ImageAccessType::ShaderRead:
                descriptor.range.aspectMask = vk::ImageAspectFlagBits::eColor;
                break;
            case ImageAccessType::DepthAttachmentRead:
            case ImageAccessType::DepthAttachmentWrite:
                descriptor.range.aspectMask = vk::ImageAspectFlagBits::eDepth;
                break;
            case ImageAccessType::None:
                switch(currentAccess) {
                    case ImageAccessType::ColorAttachmentRead:
                    case ImageAccessType::ColorAttachmentWrite:
                    case ImageAccessType::ShaderRead:
                        descriptor.range.aspectMask = vk::ImageAspectFlagBits::eColor;
                        break;
                    case ImageAccessType::DepthAttachmentRead:
                    case ImageAccessType::DepthAttachmentWrite:
                        descriptor.range.aspectMask = vk::ImageAspectFlagBits::eDepth;
                        break;
                }
        }

        if (descriptor.range.aspectMask == vk::ImageAspectFlagBits::eNone) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to infer aspect range when inserting an image barrier.");
            descriptor.range.aspectMask = vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        }

        auto dst_tuple = AccessHelper::GetAccessScope(currentAccess);
        descriptor.dst = {
            std::get<0>(dst_tuple),
            std::get<1>(dst_tuple),
            std::get<2>(dst_tuple),
            vk::QueueFamilyIgnored
        };

        auto src_tuple = AccessHelper::GetAccessScope(previousAccess);
        descriptor.src = {
            std::get<0>(src_tuple),
            std::get<1>(src_tuple),
            std::get<2>(src_tuple),
            vk::QueueFamilyIgnored
        };

        pimpl->barriers.push_back(std::move(descriptor));
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
        std::vector <vk::ImageMemoryBarrier2> barriers (pimpl->barriers.size(), vk::ImageMemoryBarrier2{});
        for (size_t i = 0; i < barriers.size(); i++) {
            const auto & barrier = pimpl->barriers.front();
            barriers[i] = vk::ImageMemoryBarrier2{
                barrier.src.stage, barrier.src.access,
                barrier.dst.stage, barrier.dst.access,
                barrier.src.layout, barrier.dst.layout,
                barrier.src.queue, barrier.dst.queue,
                barrier.image,
                barrier.range
            };
            pimpl->barriers.pop_front();
        }

        vk::DependencyInfo dep{vk::DependencyFlags{0}, {}, {}, barriers};
        this->GetCommandBuffer().GetCommandBuffer().pipelineBarrier2(dep);
    }
}
