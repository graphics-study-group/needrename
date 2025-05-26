#include "ICommandContext.h"

#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

namespace Engine {
    vk::ImageMemoryBarrier2 ICommandContext::GetImageBarrier(
        vk::Image img,
        ImageAccessType currentAccess,
        ImageAccessType previousAccess
    ) noexcept
    {
        vk::ImageMemoryBarrier2 barrier;
        barrier.image = img;
        barrier.subresourceRange = vk::ImageSubresourceRange{
            InferImageAspectFromUsage(currentAccess, previousAccess), 
            0, vk::RemainingMipLevels, 
            0, vk::RemainingArrayLayers
        };

        if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eNone) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to infer aspect range when inserting an image barrier.");
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        }

        std::tie(barrier.dstStageMask, barrier.dstAccessMask, barrier.newLayout) = AccessHelper::GetAccessScope(currentAccess);
        std::tie(barrier.srcStageMask, barrier.srcAccessMask, barrier.oldLayout) = AccessHelper::GetAccessScope(previousAccess);
        barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        return barrier;
    }
}
