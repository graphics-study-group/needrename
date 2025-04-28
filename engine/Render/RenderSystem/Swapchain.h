#ifndef RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
#define RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED

#include "Render/VkWrapper.tcc"
#include "Structs.h"
#include "Render/Memory/Image2D.h"
#include <vector>

namespace Engine {

    class RenderSystem;

    namespace RenderSystemState {
        class PhysicalDevice;
        class Swapchain {
        protected:
            vk::UniqueSwapchainKHR m_swapchain {};
            // Images retreived from swapchain don't require clean up.
            std::vector <vk::Image> m_images {};
            // However, image views do need clean up.
            std::vector <vk::UniqueImageView> m_image_views {};

            vk::SurfaceFormatKHR m_image_format {};
            vk::Extent2D m_extent {};

            /// @brief Select a swap chain config from all supported ones
            /// @param support 
            /// @param expected_extent
            /// @return std::tuple <vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR>
            static std::tuple <vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR>
            SelectSwapchainConfig(const SwapchainSupport & support, vk::Extent2D expected_extent);

            void RetrieveImageViews(vk::Device device);
        public:
            static constexpr auto DEPTH_FORMAT = ImageUtils::ImageFormat::D32SFLOAT;

            void CreateSwapchain(const PhysicalDevice & physical_device, 
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
    }
}

#endif // RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
