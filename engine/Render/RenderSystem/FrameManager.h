#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

// May be safe to include here as this header is not included in other headers.
#include <vulkan/vulkan.hpp>

#include "Render/Memory/MemoryAccessTypes.h"

namespace Engine {
    class RenderSystem;
    class Texture;
    class DeviceBuffer;
    class GraphicsCommandBuffer;
    class GraphicsContext;
    class ComputeContext;

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
            GraphicsCommandBuffer GetCommandBuffer();

            /// @deprecated
            [[deprecated]]
            GraphicsContext GetGraphicsContext();

            /// @deprecated
            [[deprecated]]
            ComputeContext GetComputeContext();

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

            /// Buffer Readback operations

            /**
             * @brief Enqueue a post graphics readback from GPU to CPU host memory.
             *
             * While readback commands are submitted to GPU on main commandbuffer submission,
             * data will not be available until the frame has completed.
             *
             * This method performs no sychronization operation apart from the semaphore
             * wait on the submission of the readback commandbuffer.
             *
             * @return a staging buffer, whose content is undetermined until
             * this frame has completed.
             */
            std::shared_ptr<DeviceBuffer> EnqueuePostGraphicsBufferReadback(const DeviceBuffer &device_buffer);

            /**
             * @brief Enqueue a post graphics readback from GPU to CPU host memory.
             *
             * While readback commands are submitted to GPU on main commandbuffer submission,
             * data will not be available until the frame has completed.
             *
             * This method performs no sychronization operation apart from the semaphore
             * wait on the submission of the readback commandbuffer.
             * The texture is assumed to be in `GENERAL` layout.
             * Only color aspect is read back.
             *
             * @return a staging buffer, whose content is undetermined until
             * this frame has completed.
             */
            std::shared_ptr<DeviceBuffer> EnqueuePostGraphicsImageReadback(
                const Texture &image, uint32_t array_layer, uint32_t miplevel
            );
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
