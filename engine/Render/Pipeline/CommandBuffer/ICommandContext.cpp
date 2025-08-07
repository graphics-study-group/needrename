#include "ICommandContext.h"

#include "Render/Memory/Texture.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"
#include "Render/ImageUtilsFunc.h"

#include <vulkan/vulkan.hpp>

namespace Engine {
    vk::ImageMemoryBarrier2 ICommandContext::GetImageBarrier(
        const Texture &texture, ImageAccessType currentAccess, ImageAccessType previousAccess
    ) noexcept {
        vk::ImageMemoryBarrier2 barrier;
        barrier.image = texture.GetImage();
        barrier.subresourceRange = vk::ImageSubresourceRange{
            ImageUtils::GetVkAspect(texture.GetTextureDescription().format),
            0,
            vk::RemainingMipLevels,
            0,
            vk::RemainingArrayLayers
        };

        if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eNone) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to infer aspect range when inserting an image barrier.");
            barrier.subresourceRange.aspectMask =
                vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        }

        std::tie(barrier.dstStageMask, barrier.dstAccessMask, barrier.newLayout) =
            AccessHelper::GetAccessScope(currentAccess);
        std::tie(barrier.srcStageMask, barrier.srcAccessMask, barrier.oldLayout) =
            AccessHelper::GetAccessScope(previousAccess);
        barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        return barrier;
    }
} // namespace Engine
