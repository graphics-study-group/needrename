#include "Swapchain.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

namespace {

    vk::SurfaceFormatKHR SelectSwapchainFormat(
        const std::vector<vk::SurfaceFormatKHR> & formats
    ) {
        vk::SurfaceFormatKHR pickedFormat{};
        for (const auto &format : formats) {
            if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                // Select R8G8B8A8 SRGB first
                if (format.format == vk::Format::eR8G8B8A8Srgb) {
                    pickedFormat = format;
                    // Select B8G8R8A8 SRBG as backup
                } else if (format.format == vk::Format::eB8G8R8A8Srgb) {
                    if (pickedFormat.format != vk::Format::eR8G8B8A8Srgb) {
                        pickedFormat = format;
                    }
                }
            }
        }
        if (pickedFormat.format == vk::Format::eUndefined) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "This device support neither B8G8R8A8 nor R8G8B8A8 swapchain format.");
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Critical Error",
                "Your GPU does not support required color formats for rendering.\n"
                "This is an unrecoverable error and the program will now terminate.",
                nullptr
            );
            std::terminate();
        } else if (pickedFormat.format == vk::Format::eB8G8R8A8Srgb) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "This device does not support R8G8B8A8 swapchain format. Falling back to "
                "B8G8R8A8. Blue and red channels may appear swapped."
            );
        }
        return pickedFormat;
    }

    vk::PresentModeKHR SelectPresentMode(
        const std::vector<vk::PresentModeKHR> & modes
    ) {
        // Select display mode
        vk::PresentModeKHR pickedMode = vk::PresentModeKHR::eFifo;
        for (const auto &mode : modes) {
            if (mode == vk::PresentModeKHR::eMailbox) {
                pickedMode = mode;
                break;
            }
        }
        if (pickedMode == vk::PresentModeKHR::eFifo) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Mailbox mode not supported, fall back to FIFO.");
        }
        return pickedMode;
    }

    vk::Extent2D SelectSwapchainExtent(
        const vk::SurfaceCapabilitiesKHR & caps,
        vk::Extent2D expected_extent
    ) {
        // Measure extent
        vk::Extent2D extent{};
        if (caps.currentExtent.height != std::numeric_limits<uint32_t>::max()) {
            extent = caps.currentExtent;
        } else {
            extent = expected_extent;
            extent.width = std::clamp(
                extent.width, caps.minImageExtent.width, caps.maxImageExtent.width
            );
            extent.height = std::clamp(
                extent.height, caps.minImageExtent.height, caps.maxImageExtent.height
            );

            if (extent.width != expected_extent.width || extent.height != expected_extent.height) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Swapchain extent clamped to (%u, %u).", extent.width, extent.height);
            }
        }
        return extent;
    }
}

namespace Engine::RenderSystemState {
    void Swapchain::CreateSwapchain(
        const DeviceInterface & interface,
        vk::Extent2D expected_extent
    ) {
        const auto swapchain_support = interface.GetSwapchainSupport();
        const auto extent = SelectSwapchainExtent(swapchain_support.capabilities, expected_extent);
        const auto format = SelectSwapchainFormat(swapchain_support.formats);
        const auto mode = SelectPresentMode(swapchain_support.modes);

        uint32_t image_count = swapchain_support.capabilities.minImageCount + 1;
        if (swapchain_support.capabilities.maxImageCount > 0
            && image_count > swapchain_support.capabilities.maxImageCount) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Requested %u images in swap chain, but only %u are allowed.",
                image_count,
                swapchain_support.capabilities.maxImageCount
            );
            image_count = swapchain_support.capabilities.maxImageCount;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating a swapchain with %u images.", image_count);

        vk::SwapchainCreateInfoKHR info;
        info.surface = interface.GetSurface();
        info.minImageCount = image_count;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.presentMode = mode;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = vk::ImageUsageFlagBits::eTransferDst;
        info.preTransform = swapchain_support.capabilities.currentTransform;
        // Disable alpha blending for framebuffers
        info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        info.clipped = vk::True;
        if (m_swapchain) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Replacing old swap chain.");
            info.oldSwapchain = m_swapchain.get();
        } else {
            info.oldSwapchain = nullptr;
        }

        auto graphics_index = interface.GetQueueFamily(DeviceInterface::QueueFamilyType::GraphicsMain).value();
        std::array indices{
            graphics_index,
            interface.GetQueueFamily(DeviceInterface::QueueFamilyType::GraphicsPresent).value(),
            interface.GetQueueFamily(DeviceInterface::QueueFamilyType::AsynchronousComputePresent).value_or(graphics_index),
        };
        std::ranges::sort(indices);
        auto [ret, last] = std::ranges::unique(indices);
        if (std::distance(indices.begin(), ret) > 1) {
            info.imageSharingMode = vk::SharingMode::eConcurrent;
            info.queueFamilyIndexCount = std::distance(indices.begin(), ret);
            info.pQueueFamilyIndices = indices.data();
        } else {
            info.imageSharingMode = vk::SharingMode::eExclusive;
        }

        m_swapchain = interface.GetDevice().createSwapchainKHRUnique(info);
        m_images = interface.GetDevice().getSwapchainImagesKHR(m_swapchain.get());
        m_image_format = format;
        m_extent = extent;
    }

    vk::SwapchainKHR Swapchain::GetSwapchain() const {
        return m_swapchain.get();
    }
    auto Swapchain::GetImages() const -> const decltype(m_images) & {
        return m_images;
    }

    vk::SurfaceFormatKHR Swapchain::GetImageFormat() const {
        return m_image_format;
    }
    vk::Extent2D Swapchain::GetExtent() const {
        return m_extent;
    }

    uint32_t Swapchain::GetFrameCount() const {
        return m_images.size();
    }
    vk::ImageMemoryBarrier2 Swapchain::GetPreCopyBarrier(uint32_t framebuffer) const noexcept {
        assert(framebuffer < m_images.size());
        return vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eNone, // > Set up execution dep instead of memory dep.
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            m_images[framebuffer],
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
    }
    vk::ImageMemoryBarrier2 Swapchain::GetPostCopyBarrier(uint32_t framebuffer) const noexcept {
        assert(framebuffer < m_images.size());
        return vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            m_images[framebuffer],
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
    }
} // namespace Engine::RenderSystemState
