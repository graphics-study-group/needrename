#ifndef RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
#define RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED

#include <memory>
#include <vector>

namespace vk {
    struct SwapchainKHR;
    struct Image;
    struct SurfaceFormatKHR;
    struct Extent2D;
    struct ImageMemoryBarrier2;

    enum class Format;
}

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        class DeviceInterface;
        class Swapchain {
            struct impl;
            std::unique_ptr <impl> pimpl;

        public:
            Swapchain() noexcept;
            ~Swapchain() noexcept;

            void CreateSwapchain(
                const DeviceInterface & device_interface,
                vk::Extent2D expected_extent
            );

            vk::SwapchainKHR GetSwapchain() const noexcept;
            const std::vector <vk::Image> & GetImages() const noexcept;

            /**
             * @brief Get current format of the presentable surface.
             */
            vk::SurfaceFormatKHR GetSurfaceFormat() const noexcept;

            /**
             * @brief Get current extent of the presentable surface.
             */
            vk::Extent2D GetExtent() const noexcept;

            /**
             * @brief Get total frame buffer images count.
             */
            uint32_t GetFrameCount() const noexcept;

            /**
             * @brief Get the color format of the presentable surface.
             * 
             * Shorthand for `GetSurfaceFormat().format`.
             */
            vk::Format GetColorFormat() const noexcept;

            /**
             * @brief Get a barrier that transit the state of the given framebuffer
             * before the copy or blitting command.
             */
            vk::ImageMemoryBarrier2 GetPreCopyBarrier(uint32_t framebuffer) const noexcept;

            /**
             * @brief Get a barrier that transit the state of the given framebuffer
             * after the copy or blitting command.
             */
            vk::ImageMemoryBarrier2 GetPostCopyBarrier(uint32_t framebuffer) const noexcept;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_SWAPCHAIN_INCLUDED
