#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

#include "Render/Pipeline/CommandBuffer/RenderCommandBuffer.h"

namespace Engine {
    class RenderSystem;
    namespace RenderSystemState {
        /// @brief Multiple frame in flight manager
        class FrameManager final {
        public:
            static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

        private:
            std::array <vk::UniqueSemaphore, FRAMES_IN_FLIGHT> image_acquired_semaphores {};
            std::array <vk::UniqueSemaphore, FRAMES_IN_FLIGHT> command_executed_semaphores {};
            std::array <vk::UniqueFence, FRAMES_IN_FLIGHT> command_executed_fences {};
            std::array <vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> command_buffers {};
            std::vector <RenderCommandBuffer> render_command_buffers {};

            uint32_t current_frame_in_flight {std::numeric_limits<uint32_t>::max()};

            // Current frame buffer id. Set by `StartFrame()` method.
            uint32_t current_framebuffer {std::numeric_limits<uint32_t>::max()};

            vk::Queue present_queue {};
            vk::SwapchainKHR swapchain {};
            std::weak_ptr <RenderSystem> m_system {};
        public:
            void Create (std::shared_ptr <RenderSystem> system);

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
             * Wait for the previous command buffer of the same frame in flight counter to finish execution,
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
             * @brief Announce the completion of CPU works of this frame in flight.
             * Queue a present command and increment FIF counter.
             * 
             * Ensure the command buffer is submitted to the queue before presenting.
             * Submitting the command buffer commences the GPU side of the rendering asynchronously.
             * Use fence to ensure that the command buffer execution is finished.
             */
            void CompleteFrame ();
        };
    }
}

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
