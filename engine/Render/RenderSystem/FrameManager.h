#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

#include <functional>
// May be safe to include here as this header is not included in other headers.
#include <vulkan/vulkan.hpp>

#include "Render/Memory/MemoryAccessTypes.h"

namespace Engine {
    class RenderSystem;
    class Texture;
    class DeviceBuffer;
    class CommandBuffer;

    namespace RenderSystemState {
        class SubmissionHelper;
        class FrameSemaphore;

        /// @brief Multiple frame in flight manager
        class FrameManager final {
        public:
            /**
             * @brief Expected frames-in-flight of the current application.
             *
             * Controls to what degree CPU codes can _overtake_ GPU codes. For
             * a default setting of 3, CPU can record commands 3 frames ahead
             * of the GPU. In general, higher value improves throughput but
             * makes latency larger.
             *
             * This number may be different from the swapchain image counts.
             */
            static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

        private:
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            FrameManager(RenderSystem &sys);
            ~FrameManager();

            /**
             * @brief Create the frame manager.
             *
             * Allocate synchronization primitives such as fences and semaphores.
             * Also allocates reused command buffers (i.e main command buffers).
             */
            void Create();

            /// @brief Get the current frame-in-flight count.
            uint32_t GetFrameInFlight() const noexcept;
            /// @brief Get the current frame count.
            uint64_t GetTotalFrame() const noexcept;

            /**
             * @brief Get the current free image index in the swapchain.
             *
             * @note This method is in general only used by render system
             * internally when presenting & interacting with the OS.
             * You might be looking for `GetFrameInFlight()`.
             */
            uint32_t GetFramebuffer() const noexcept;

            /**
             * @brief Get the command buffer assigned to the current frame in flight.
             *
             * Must be called between `StartFrame()` and `CompleteFrame()`
             */
            [[deprecated]]
            CommandBuffer GetCommandBuffer();

            /**
             * @brief Request the current main command buffer
             */
            vk::CommandBuffer GetRawMainCommandBuffer();

            /**
             * @brief Start the rendering of a new frame.
             *
             * Wait for the
             * previous _copy_ command buffer of the same frame in flight counter to finish execution,
             *
             * reset corresponding command buffer and fence and acquire a new image on the swapchain that
             * is ready for rendering.
             *
             * @param timeout timeout in milliseconds
             *
             * @return The index of the available image on the swapchain,
             * which is used to determine
             * which framebuffer to render to.
             * @note The index of the available image might be different
             * from the counter of the current frame in flight.
             */
            uint32_t StartFrame(uint64_t timeout = std::numeric_limits<uint64_t>::max());

            /**
             * @brief Submit the main command buffer to the graphics queue for execution.
             *
             * Also performs staged resource submission.
             *
             * @warning Must be called before `PresentToFramebuffer`, once each frame.
             *
             * Non-standard call-sites are likely to result in deadlocks and vulkan device losses.
             */
            void SubmitMainCommandBuffer();

            /**
             * @brief Present an image to the swapchain by blitting.
             * The area specified by extent and offset will be blitted
             * to the whole swapchain image.
             *
             * This method inserts appopriate barriers to ensure memory dependencies for the image.
             * The layout of the image is assumed to be in `COLOR_ATTACHMENT_OPTIMAL`.
             *
             * Exactly one call of this method is expected each frame.
             * You should probably use `RenderSystem::CompleteFrame()`
             * if you have no idea what to use.
             *
             * @return True if the swapchain needs to be recreated.
             *
             * @todo Revisit sychronization methods for this command.
             */
            [[nodiscard]]
            bool PresentToFramebuffer(
                vk::Image image,
                MemoryAccessTypeImageBits last_access,
                vk::Extent2D extentSrc,
                vk::Offset2D offsetSrc = {0, 0},
                vk::Filter filter = vk::Filter::eLinear
            );

            /// @brief Get the submission helper.
            SubmissionHelper &GetSubmissionHelper();

            /// @brief Get the current frame semaphore.
            const FrameSemaphore &GetFrameSemaphore() const noexcept;

            /**
             * @brief Function type of callback of readback operations.
             *
             * Data retrieved from the device is stored in the device buffer.
             * This buffer can be mapped to the host VM for reading.
             */
            using ReadbackCallback = std::function<void(std::unique_ptr<DeviceBuffer>)>;
            /**
             * @brief Register a callback for buffer or texture readback.
             *
             * The callback is associated with the current frame-in-flight.
             * After all command buffers in the current frame-in-flight are
             * completed, a special command buffer containing copying commands
             * will be executed.
             *
             * On completion of subsequent frames, registered callbacks will be
             * executed if the copying is completed.
             * Typically a delay of one to two frames can be expected.
             *
             * This readback supports buffer only to avoid dealing with layout
             * transition problem. Issue a copy to buffer command in your
             * rendering loop to copy your texture to a buffer first.
             *
             * @return Whether the callback can be added to current frame-in-flight.
             * If too many readback requests are not fulfilled, the registering
             * might fail.
             */
            bool RegisterReadbackCallback(const DeviceBuffer &buffer, ReadbackCallback cb);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
