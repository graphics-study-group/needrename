#include "Swapchain.h"
#include "Render/RenderSystem/PhysicalDevice.h"
#include <SDL3/SDL.h>

namespace Engine::RenderSystemState{
    std::tuple<vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR> 
    Swapchain::SelectSwapchainConfig(const SwapchainSupport& support, vk::Extent2D expected_extent) {
        assert(!support.formats.empty());
        assert(!support.modes.empty());
        // Select surface format
        vk::SurfaceFormatKHR pickedFormat = support.formats[0];
        for (const auto & format : support.formats) {
            if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear 
                && format.format == vk::Format::eB8G8R8A8Srgb) {
                pickedFormat = format;
            }
        }
        // Select display mode
        vk::PresentModeKHR pickedMode = vk::PresentModeKHR::eFifo;
        for (const auto & mode : support.modes) {
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

            extent.width = std::clamp(extent.width, 
                support.capabilities.minImageExtent.width,
                support.capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height, 
                support.capabilities.minImageExtent.height, 
                support.capabilities.maxImageExtent.height);

            if (extent.width != expected_extent.width || extent.height != expected_extent.height) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Extent clamped to (%u, %u).", extent.width, extent.height);
            }
        }
        return std::make_tuple(extent, pickedFormat, pickedMode);
    }

    void Swapchain::RetrieveImageViews(vk::Device device) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, 
            "Retreiving image views for %llu swap chain images.", 
            m_images.size()
        );
        m_image_views.clear();
        m_image_views.resize(m_images.size());
        for (size_t i = 0; i < m_images.size(); i++) {
            vk::ImageViewCreateInfo info;
            info.image = m_images[i];
            info.viewType = vk::ImageViewType::e2D;
            info.format = m_image_format.format;
            info.components.r = vk::ComponentSwizzle::eIdentity;
            info.components.g = vk::ComponentSwizzle::eIdentity;
            info.components.b = vk::ComponentSwizzle::eIdentity;
            info.components.a = vk::ComponentSwizzle::eIdentity;
            info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount = 1;
            info.subresourceRange.baseMipLevel = 0;
            info.subresourceRange.levelCount = 1;
            m_image_views[i] = device.createImageViewUnique(info);
        }
    }

    void Swapchain::CreateSwapchain(const PhysicalDevice & physical_device, 
        vk::Device logical_device, 
        vk::SurfaceKHR surface, 
        vk::Extent2D expected_extent
    ) {
        const auto swapchain_support = physical_device.GetSwapchainSupport();
        const auto [extent, format, mode] = SelectSwapchainConfig(swapchain_support, expected_extent);

        uint32_t image_count = swapchain_support.capabilities.minImageCount + 1;
        if (swapchain_support.capabilities.maxImageCount > 0 && image_count > swapchain_support.capabilities.maxImageCount) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, 
                "Requested %u images in swap chain, but only %u are allowed.", 
                image_count, swapchain_support.capabilities.maxImageCount);
            image_count = swapchain_support.capabilities.maxImageCount;
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating a swapchain with %u images.", image_count);

        vk::SwapchainCreateInfoKHR info;
        info.surface = surface;
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

        auto indices = physical_device.GetQueueFamilyIndices();
        std::vector <uint32_t> queues {indices.graphics.value(), indices.present.value()};
        if (indices.graphics != indices.present) {
            info.imageSharingMode = vk::SharingMode::eConcurrent;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = queues.data();
        } else {
            info.imageSharingMode = vk::SharingMode::eExclusive;
        }
        
        m_swapchain = logical_device.createSwapchainKHRUnique(info);
        m_images = logical_device.getSwapchainImagesKHR(m_swapchain.get());
        m_image_format = format;
        m_extent = extent;

        this->RetrieveImageViews(logical_device);
    }

    void Swapchain::DisableDepthTesting() {
        m_depth_images.clear();
        is_depth_enabled = false;
    }

    void Swapchain::EnableDepthTesting(std::weak_ptr<RenderSystem> system) {
        assert(m_depth_images.empty());
        m_depth_images.reserve(m_images.size());
        for (size_t i = 0; i < m_images.size(); i++) {
            m_depth_images.emplace_back(system);
            m_depth_images[i].Create(
                m_extent.width, 
                m_extent.height, 
                AllocatedImage2D::ImageType::DepthImage,
                DEPTH_FORMAT
            );
        }
        m_depth_images.shrink_to_fit();
        is_depth_enabled = true;
    }

    vk::SwapchainKHR Swapchain::GetSwapchain() const {
        return m_swapchain.get();
    }
    auto Swapchain::GetImages() const -> const decltype(m_images)& {
        return m_images;
    }
    auto Swapchain::GetImageViews() const -> const decltype(m_image_views)& {
        return m_image_views;
    }
    auto Swapchain::GetDepthImages() const -> const decltype(m_depth_images)& {
        return m_depth_images;
    }
    vk::SurfaceFormatKHR Swapchain::GetImageFormat() const { return m_image_format; }
    vk::Extent2D Swapchain::GetExtent() const { return m_extent; }
    bool Swapchain::IsDepthEnabled() const {
        return is_depth_enabled;
    }

    uint32_t Swapchain::GetFrameCount() const
    {
        assert((!is_depth_enabled && m_depth_images.size() == 0) 
            || (is_depth_enabled && m_images.size() == m_depth_images.size()));
        assert(m_images.size() == m_image_views.size());
        return m_images.size();
    }

    SwapchainImage Swapchain::GetDepthImagesAndViews() const
    {
        std::vector <vk::Image> images;
        std::vector <vk::ImageView> image_views;
        for (size_t i = 0; i < m_depth_images.size(); i++) {
            images.push_back(m_depth_images[i].GetImage());
            image_views.push_back(m_depth_images[i].GetImageView());
        }
        return SwapchainImage(images, image_views);
    }

    SwapchainImage Swapchain::GetColorImagesAndViews() const
    {
        std::vector <vk::Image> images;
        std::vector <vk::ImageView> image_views;
        for (size_t i = 0; i < m_images.size(); i++) {
            images.push_back(m_images[i]);
            image_views.push_back(m_image_views[i].get());
        }
        return SwapchainImage(images, image_views);
    }
}
