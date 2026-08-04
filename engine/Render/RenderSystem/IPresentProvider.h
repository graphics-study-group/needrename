#ifndef RENDER_RENDERSYSTEM_IPRESENTPROVIDER_INCLUDED
#define RENDER_RENDERSYSTEM_IPRESENTPROVIDER_INCLUDED

#include <array>
#include <vulkan/vulkan.hpp>

namespace Engine {
    class RenderTargetTexture;

    /**
     * @brief Synchronization primitives for a frame completion submit.
     *
     * Built by FrameManager from its internal timeline / binary semaphores
     * and fence. The provider passes these directly into `vk::SubmitInfo2`
     * without interpreting their semantics.
     */
    struct FrameSyncInfo {
        std::array<vk::SemaphoreSubmitInfo, 2> wait{};
        std::array<vk::SemaphoreSubmitInfo, 2> signal{};
        /// @brief Binary semaphore the present operation waits on.
        /// Signaled by the copy submit; must equal `signal[0]`.
        vk::Semaphore present_wait_semaphore;
        vk::Fence fence;
    };

    /**
     * @brief Abstraction of the swapchain-backed presentation backend.
     *
     * Owns swapchain resources and executes acquire/present operations,
     * while FrameManager owns all frame synchronization state.
     */
    class IPresentProvider {
    public:
        virtual ~IPresentProvider() = default;

        virtual vk::Extent2D GetExtent() const = 0;
        virtual vk::Format GetColorFormat() const = 0;
        virtual uint32_t GetImageCount() const = 0;

        /// @brief Acquire the next image to render into.
        virtual uint32_t AcquireNextImage(vk::Device device, vk::Semaphore image_ready_semaphore, uint64_t timeout) = 0;

        /**
         * @brief Complete the current frame: record blit, submit, and present.
         *
         * @return true if the swapchain needs to be recreated.
         */
        virtual bool CompleteFrame(
            vk::Device device, const RenderTargetTexture &final_rtt, uint32_t image_index, const FrameSyncInfo &sync
        ) = 0;

        virtual void Recreate(vk::Extent2D new_extent) = 0;
    };
} // namespace Engine

#endif
