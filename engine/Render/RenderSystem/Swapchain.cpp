#include "Swapchain.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

std::tuple<vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR> SelectSwapchainConfig(
    const Engine::RenderSystemState::SwapchainSupport &support, vk::Extent2D expected_extent
) {
    assert(!support.formats.empty());
    assert(!support.modes.empty());
    // Select surface format
    vk::SurfaceFormatKHR pickedFormat{};
    for (const auto &format : support.formats) {
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
    } else if (pickedFormat.format == vk::Format::eB8G8R8A8Srgb) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_RENDER,
            "This device does not support R8G8B8A8 swapchain format. Falling back to "
            "B8G8R8A8. Blue and red channels may appear swapped."
        );
    }
    // Select display mode
    vk::PresentModeKHR pickedMode = vk::PresentModeKHR::eFifo;
    for (const auto &mode : support.modes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            pickedMode = mode;
            break;
        }
    }
    if (pickedMode == vk::PresentModeKHR::eFifo) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Mailbox mode not supported, fall back to FIFO.");
    }

    // Measure extent
    vk::Extent2D extent{};
    if (support.capabilities.currentExtent.height != std::numeric_limits<uint32_t>::max()) {
        extent = support.capabilities.currentExtent;
    } else {
        extent = expected_extent;

        extent.width = std::clamp(
            extent.width, support.capabilities.minImageExtent.width, support.capabilities.maxImageExtent.width
        );
        extent.height = std::clamp(
            extent.height, support.capabilities.minImageExtent.height, support.capabilities.maxImageExtent.height
        );

        if (extent.width != expected_extent.width || extent.height != expected_extent.height) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Swapchain extent clamped to (%u, %u).", extent.width, extent.height);
        }
    }
    return std::make_tuple(extent, pickedFormat, pickedMode);
}

namespace Engine::RenderSystemState {
    void Swapchain::RetrieveImageViews(vk::Device device) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Retreiving image views for %llu swap chain images.", m_images.size());
        m_image_views.clear();
        m_image_views.resize(m_images.size());

        vk::ImageViewCreateInfo ivci{
            vk::ImageViewCreateFlags{},
            nullptr,
            vk::ImageViewType::e2D,
            vk::Format::eR8G8B8A8Srgb,
            m_image_format.format == vk::Format::eR8G8B8A8Srgb
                ? vk::ComponentMapping{}
                : vk::ComponentMapping{vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eIdentity},
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };

        for (size_t i = 0; i < m_images.size(); i++) {
            ivci.image = m_images[i];
            m_image_views[i] = device.createImageViewUnique(ivci);
        }
    }

    void Swapchain::CreateSwapchain(
        const DeviceInterface & interface,
        vk::Extent2D expected_extent
    ) {
        const auto swapchain_support = interface.GetSwapchainSupport();
        const auto [extent, format, mode] = SelectSwapchainConfig(swapchain_support, expected_extent);

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
        info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
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

        auto indices = interface.GetQueueFamilies();
        std::vector<uint32_t> queues{indices.graphics.value(), indices.present.value()};
        if (indices.graphics != indices.present) {
            info.imageSharingMode = vk::SharingMode::eConcurrent;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = queues.data();
        } else {
            info.imageSharingMode = vk::SharingMode::eExclusive;
        }

        m_swapchain = interface.GetDevice().createSwapchainKHRUnique(info);
        m_images = interface.GetDevice().getSwapchainImagesKHR(m_swapchain.get());
        m_image_format = format;
        m_extent = extent;

        this->RetrieveImageViews(interface.GetDevice());
    }

    vk::SwapchainKHR Swapchain::GetSwapchain() const {
        return m_swapchain.get();
    }
    auto Swapchain::GetImages() const -> const decltype(m_images) & {
        return m_images;
    }
    auto Swapchain::GetImageViews() const -> const decltype(m_image_views) & {
        return m_image_views;
    }

    vk::SurfaceFormatKHR Swapchain::GetImageFormat() const {
        return m_image_format;
    }
    vk::Extent2D Swapchain::GetExtent() const {
        return m_extent;
    }

    uint32_t Swapchain::GetFrameCount() const {
        assert(m_images.size() == m_image_views.size());
        return m_images.size();
    }
} // namespace Engine::RenderSystemState
