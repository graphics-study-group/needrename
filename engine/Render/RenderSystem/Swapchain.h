#ifndef RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
#define RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED

#include "Render/ImageUtils.h"
#include "Render/VkWrapper.tcc"
#include <vector>

namespace Engine {

    class RenderSystem;

    namespace RenderSystemState {
        class PhysicalDevice;
        class Swapchain {
        protected:
            vk::UniqueSwapchainKHR m_swapchain{};
            // Images retreived from swapchain don't require clean up.
            std::vector<vk::Image> m_images{};
            // However, image views do need clean up.
            std::vector<vk::UniqueImageView> m_image_views{};

            vk::SurfaceFormatKHR m_image_format{};
            vk::Extent2D m_extent{};

            void RetrieveImageViews(vk::Device device);

        public:
            static constexpr auto COLOR_FORMAT_VK = vk::Format::eR8G8B8A8Srgb;
            static constexpr auto COLOR_FORMAT = ImageUtils::ImageFormat::R8G8B8A8SRGB;
            static constexpr auto DEPTH_FORMAT = ImageUtils::ImageFormat::D32SFLOAT;

            void CreateSwapchain(
                const PhysicalDevice &physical_device,
                vk::Device logical_device,
                vk::SurfaceKHR surface,
                vk::Extent2D expected_extent
            );

            vk::SwapchainKHR GetSwapchain() const;
            auto GetImages() const -> const decltype(m_images) &;
            auto GetImageViews() const -> const decltype(m_image_views) &;

            vk::SurfaceFormatKHR GetImageFormat() const;
            vk::Extent2D GetExtent() const;

            uint32_t GetFrameCount() const;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
