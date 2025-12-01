#ifndef RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
#define RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED

#include "Render/ImageUtils.h"
#include <vector>

namespace Engine {

    class RenderSystem;

    namespace RenderSystemState {
        class DeviceInterface;
        class Swapchain {
        protected:
            vk::UniqueSwapchainKHR m_swapchain{};
            // Images retreived from swapchain don't require clean up.
            std::vector<vk::Image> m_images{};

            vk::SurfaceFormatKHR m_image_format{};
            vk::Extent2D m_extent{};

        public:
            static constexpr auto COLOR_FORMAT_VK = vk::Format::eR8G8B8A8Srgb;
            static constexpr auto DEPTH_FORMAT_VK = vk::Format::eD32Sfloat;

            void CreateSwapchain(
                const DeviceInterface & device_interface,
                vk::Extent2D expected_extent
            );

            vk::SwapchainKHR GetSwapchain() const;
            auto GetImages() const -> const decltype(m_images) &;

            vk::SurfaceFormatKHR GetImageFormat() const;
            vk::Extent2D GetExtent() const;

            uint32_t GetFrameCount() const;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
