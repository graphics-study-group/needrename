#ifndef RENDER_RENDERSYSTEM_IPRESENTPROVIDER_INCLUDED
#define RENDER_RENDERSYSTEM_IPRESENTPROVIDER_INCLUDED

#include "Rhi/Device/MemoryAccessTypes.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>

namespace Engine {
    class RenderTargetTexture;

    /**
     * @brief Abstraction of the presentation backend (windowed swapchain or
     * headless).
     *
     * The interface is pure presentation: acquire a target, record the copy
     * blit, present. FrameManager owns ALL frame-lifecycle synchronization
     * (timeline semaphores, acquire semaphores, fences, frame-in-flight
     * counter) and executes the frame-completion submit itself. The only
     * synchronization object crossing this interface is the frame completion
     * credential (a binary semaphore) passed into `Present` — FrameManager
     * lends it, the provider consumes it, meaning "present must wait until
     * this frame (including the copy) has completed".
     */
    class IPresentProvider {
    public:
        virtual ~IPresentProvider() = default;

        /**
         * @brief Get the extent (in pixels) of the presentation target.
         *
         * @return Windowed: the current swapchain extent; headless: the extent
         * configured at construction (updated by `Recreate`).
         */
        virtual vk::Extent2D GetExtent() const = 0;

        /**
         * @brief Get the color format of the presentation target.
         *
         * @return The surface format for windowed mode, or the format passed
         * at construction for headless mode.
         */
        virtual vk::Format GetColorFormat() const = 0;

        /**
         * @brief Get the number of presentation target images.
         *
         * Windowed: the swapchain image count; headless: the configured image
         * count (typically `FRAMES_IN_FLIGHT`). Used by FrameManager to size
         * per-image resources.
         *
         * @return The image count of the presentation target.
         */
        virtual uint32_t GetImageCount() const = 0;

        /**
         * @brief Acquire the next presentation target index.
         *
         * Async contract: when the returned image becomes available for
         * writing, the implementation MUST signal `image_ready_semaphore`
         * (windowed: signaled by `vkAcquireNextImageKHR`; headless: signaled
         * by an empty submit). The CPU never blocks here.
         *
         * @param device                 The Vulkan logical device used for
         *                               acquisition.
         * @param image_ready_semaphore  Binary semaphore that MUST be signaled
         *                               when the returned image is available for
         *                               writing. The frame-completion batch
         *                               waits on it at the `eAllTransfer` stage.
         * @param timeout                Timeout in nanoseconds (not
         *                               milliseconds) for the acquisition;
         *                               `UINT64_MAX` waits indefinitely.
         * @return The acquired image index, or `~0u` when the swapchain is
         * out of date and must be recreated before retrying.
         */
        virtual uint32_t AcquireNextImage(vk::Device device, vk::Semaphore image_ready_semaphore, uint64_t timeout) = 0;

        /**
         * @brief Record the blit (final RTT → presentation target) into the
         * implementation's own copy command buffer (one per image) and return
         * it.
         *
         * The returned buffer is submitted by FrameManager as part of the
         * frame-completion batch; it is NOT submitted here. The buffer is
         * reused across frames — safe because the target image is only
         * re-acquired after its previous present completed.
         *
         * @param device       The Vulkan logical device used to record the copy commands.
         * @param final_rtt    The final render target texture to blit into the presentation target.
         * @param image_index  Index of the presentation target image to blit into; also selects which copy command buffer to reuse.
         * @param last_access  Access mode of `final_rtt` in its last pass
         *                     (e.g. `ShaderRandomWrite` after a compute pass);
         *                     used to derive the source-layout barrier via
         *                     `GetImageLayout`.
         * @return The recorded command buffer, or `nullptr` when no copy is
         * needed (headless).
         */
        virtual vk::CommandBuffer PrepareCopy(
            vk::Device device,
            const RenderTargetTexture &final_rtt,
            uint32_t image_index,
            Rhi::MemoryAccessTypeImageBits last_access
        ) = 0;

        /**
         * @brief Present the target image.
         *
         * Waits on `frame_done_semaphore` (the frame completion credential,
         * signaled by the frame-completion batch) before presenting, so the
         * presented content is guaranteed complete.
         *
         * @param device                The Vulkan logical device used for presentation.
         * @param image_index           Index of the presentation target image to present.
         * @param frame_done_semaphore  Binary semaphore signaled by the
         *                              frame-completion batch when this frame
         *                              (including the copy) has completed; the
         *                              present waits on it.
         * @return `true` if the swapchain needs recreation (`OUT_OF_DATE` /
         * `SUBOPTIMAL`). Headless always returns `false`.
         */
        virtual bool Present(vk::Device device, uint32_t image_index, vk::Semaphore frame_done_semaphore) = 0;

        /**
         * @brief Recreate the presentation target with a new extent.
         *
         * Windowed: destroys and rebuilds the swapchain (and its per-image
         * copy command buffers) at the new extent. Headless: just updates the
         * stored extent.
         *
         * @param new_extent The new extent in pixels (e.g. from a window
         *                   resize).
         */
        virtual void Recreate(vk::Extent2D new_extent) = 0;
    };
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_IPRESENTPROVIDER_INCLUDED
