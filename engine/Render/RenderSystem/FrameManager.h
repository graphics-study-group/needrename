#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

// May be safe to include here as this header is not included in other headers.
#include <vulkan/vulkan.hpp>

namespace Engine {
    class RenderSystem;
    class Texture;
    class GraphicsCommandBuffer;
    class GraphicsContext;
    class ComputeContext;

    namespace RenderSystemState {
        class SubmissionHelper;
        class FrameSemaphore;

        /// @brief Multiple frame in flight manager
        class FrameManager final {
        public:
            static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

        private:
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            FrameManager(RenderSystem &sys);
            ~FrameManager();

            void Create();

            uint32_t GetFrameInFlight() const noexcept;

            uint64_t GetTotalFrame() const noexcept;

            uint32_t GetFramebuffer() const noexcept;

            /**
             * @brief Get the command buffer assigned to the current frame in flight.
             *
             * Must be called between `StartFrame()` and `CompleteFrame()`
             */
            [[deprecated]]
            GraphicsCommandBuffer GetCommandBuffer();

            GraphicsContext GetGraphicsContext();

            ComputeContext GetComputeContext();

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
             * Exactly one call of this method is expected each frame.
             * You should probably use `RenderSystem::CompleteFrame()`
             * if you have no idea what to use.
             * 
             * @return True if the swapchain needs to be recreated.
             */
            [[nodiscard]]
            bool PresentToFramebuffer(
                vk::Image image,
                vk::Extent2D extentSrc,
                vk::Offset2D offsetSrc = {0, 0},
                vk::Filter filter = vk::Filter::eLinear
            );

            SubmissionHelper & GetSubmissionHelper();

            const FrameSemaphore & GetFrameSemaphore() const noexcept;

            /// Buffer Readback operations

            /**
             * @brief Enqueue a post graphics readback from GPU to CPU host memory.
             * 
             * While readback commands are submitted to GPU on main commandbuffer submission,
             * data will not be available until the frame has completed.
             * 
             * This method performs no sychronization operations apart from the semaphore
             * wait on the submission of the readback commandbuffer.
             * 
             * @return a staging buffer, whose content is undetermined until
             * this frame has completed.
             */
            std::shared_ptr<Buffer> EnqueuePostGraphicsBufferReadback(const Buffer & device_buffer);

            /**
             * @brief Enqueue a post graphics readback from GPU to CPU host memory.
             * 
             * While readback commands are submitted to GPU on main commandbuffer submission,
             * data will not be available until the frame has completed.
             * 
             * This method performs no sychronization operations apart from the semaphore
             * wait on the submission of the readback commandbuffer.
             * The texture is assumed to be in `TRANSFER_SRC_OPTIMAL` layout.
             * Only color aspect is read back.
             * 
             * @return a staging buffer, whose content is undetermined until
             * this frame has completed.
             */
            std::shared_ptr<Buffer> EnqueuePostGraphicsImageReadback(const Texture & image, uint32_t array_layer, uint32_t miplevel);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
