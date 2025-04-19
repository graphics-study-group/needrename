#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

#include "Render/Pipeline/CommandBuffer/RenderCommandBuffer.h"
#include "Render/RenderSystem/SubmissionHelper.h"

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        /// @brief Multiple frame in flight manager
        class FrameManager final {
        public:
            static constexpr uint32_t FRAMES_IN_FLIGHT = 3;
        private:
            struct PresentingHelper;

            std::array <vk::UniqueSemaphore, FRAMES_IN_FLIGHT> image_acquired_semaphores {};
            std::array <vk::UniqueSemaphore, FRAMES_IN_FLIGHT> render_command_executed_semaphores {};
            std::array <vk::UniqueSemaphore, FRAMES_IN_FLIGHT> copy_to_swapchain_completed_semaphores {};
            std::array <vk::UniqueSemaphore, FRAMES_IN_FLIGHT> next_frame_ready_semaphores {};
            std::array <vk::UniqueFence, FRAMES_IN_FLIGHT> command_executed_fences {};
            std::array <vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> command_buffers {};
            std::array <vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> copy_to_swapchain_command_buffers {};

            std::vector <RenderCommandBuffer> render_command_buffers {};

            uint32_t current_frame_in_flight {std::numeric_limits<uint32_t>::max()};

            // Current frame buffer id. Set by `StartFrame()` method.
            uint32_t current_framebuffer {std::numeric_limits<uint32_t>::max()};

            vk::Queue graphic_queue {};
            vk::Queue present_queue {};
            vk::SwapchainKHR swapchain {};
            RenderSystem & m_system;

            std::unique_ptr <SubmissionHelper> m_submission_helper {};
            std::unique_ptr <PresentingHelper> m_presenting_helper {};

        public:
            FrameManager (RenderSystem & sys);
            ~FrameManager ();

            void Create ();

            uint32_t GetFrameInFlight() const noexcept;
            uint32_t GetFramebuffer() const noexcept;
            
            /**
             * @brief Get the command buffer assigned to the current frame in flight.
             * Must be called between `StartFrame()` and `CompleteFrame()`
             */
            RenderCommandBuffer & GetCommandBuffer ();

            std::vector <RenderCommandBuffer> & GetCommandBuffers ();

            /**
             * @brief Start the rendering of a new frame.
             * 
             * Wait for the previous _copy_ command buffer of the same frame in flight counter to finish execution,
             * reset corresponding command buffer and fence and acquire a new image on the swapchain that 
             * is ready for rendering.
             * 
             * @param timeout timeout in milliseconds
             * @return The index of the available image on the swapchain,
             * which is used to determine which framebuffer to render to.
             * @note The index of the available image might be different from the counter of the current frame in flight.
             */
            uint32_t StartFrame (uint64_t timeout = std::numeric_limits<uint64_t>::max());

            /**
             * @brief Copy the given image to current acquired framebuffer.
             * The image must be in Color Attachment Optimal layout, which should be guaranteed so long as
             * this method is called immediately after a draw call to copy a color attachment to the framebuffer.
             * 
             * The method transits the image to Transfer Source Layout and the framebuffer to Transfer Destination Layout,
             * record a image copy command (not blitting command, so resizing is not possible), and transits the image
             * back to Color Attachment Optimal layout.
             */
            void CopyToFrameBuffer (vk::Image image, vk::Extent2D extent, vk::Offset2D offsetSrc = {0, 0}, vk::Offset2D offsetDst = {0, 0});

            /**
             * @brief Copy the given image to current acquired framebuffer.
             * This overload executes the copy with zero offset and the swapchain extent.
             * See the complete overload for more information.
             */
            void CopyToFramebuffer (vk::Image image);

            /**
             * @brief Present the swapchain image, and announce the completion of CPU works of this frame in flight.
             * 
             * Record and submit the command buffer for frame image copying, then queue a 
             * present command and increment the FIF counter.
             * 
             * The command buffer used for copying is distinct from the render command buffer, but is submitted into the
             * same graphic queue. Execution is halted before the semaphore marking the completion of the render command
             * buffer is signaled. Only after its execution is completed, semaphores for presenting and rendering for the
             * next frame will be signaled, which means only exactly one render command buffer can be executed at the same time.
             * A fence is signaled after this command buffer has finished execution. The same fence is used to control render
             * command buffer acquisition for this frame, c.f. `StartFrame()`.
             * 
             * Ensure the command buffer is submitted to the queue before presenting.
             * Submitting the command buffer commences the GPU side of the rendering asynchronously.
             * Use fence to ensure that the command buffer execution is finished.
             */
            void CompleteFrame ();

            SubmissionHelper & GetSubmissionHelper();
        };
    }
}

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
