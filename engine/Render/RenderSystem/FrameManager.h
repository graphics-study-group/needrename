#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

namespace Engine {
    class RenderSystem;
    class GraphicsCommandBuffer;
    class GraphicsContext;

    namespace RenderSystemState {
        class SubmissionHelper;
        /// @brief Multiple frame in flight manager
        class FrameManager final {
        public:
            static constexpr uint32_t FRAMES_IN_FLIGHT = 3;
        private:
            struct impl;
            std::unique_ptr <impl> pimpl;

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
            [[deprecated]]
            GraphicsCommandBuffer GetCommandBuffer ();

            GraphicsContext GetGraphicsContext();

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
             * @brief Submit the main command buffer to the graphics queue for execution.
             * 
             * @warning Must be called before either `CompositeToFramebufferAndPresent` or `CompositeToImage`, once each frame.
             * Non-standard call-sites are likely to result in deadlocks and vulkan device losses.
             */
            void SubmitMainCommandBuffer();

            /**
             * @brief Stage a composition by copy of the given image to an undetermined image.
             * The image must be in Color Attachment Optimal layout, which should be guaranteed so long as
             * this method is called immediately after a draw call to copy a color attachment to the framebuffer.
             * 
             * The method transits the image to Transfer Source Layout and the framebuffer to Transfer Destination Layout,
             * record a image copy command (not blitting command, so resizing is not possible), and transits the image
             * back to Color Attachment Optimal layout.
             */
            void StageCopyComposition (vk::Image image, vk::Extent2D extent, vk::Offset2D offsetSrc = {0, 0}, vk::Offset2D offsetDst = {0, 0});

            /**
             * @brief Stage a composition by copy of the given image to an undetermined image.
             * This overload executes the copy with zero offset and the swapchain extent.
             * See the complete overload for more information.
             */
            void StageCopyComposition (vk::Image image);
            
            /**
             * @brief Execute staged composition operation to the framebuffer, and equeue a present command.
             * 
             * Record and submit the command buffer for frame image copying.
             * 
             * The command buffer used for copying is distinct from the render command buffer, but is submitted into the
             * same graphic queue. Execution is halted before the semaphore marking the completion of the render command
             * buffer is signaled. Only after its execution is completed, semaphores for presenting and rendering for the
             * next frame will be signaled, which means only exactly one render command buffer can be executed at the same time.
             * A fence is signaled after this command buffer has finished execution. The same fence is used to control render
             * command buffer acquisition for this frame, c.f. `StartFrame()`.
             * 
             * @warning This should be the last method to be called each frame, and it will progress the internal state machine.
             * Either `CompositeToFramebufferAndPresent()` or `CompositeToImage()` must be called exactly once each frame.
             * Non-standard call-sites are likely to result in deadlocks and vulkan device losses.
             * 
             * @return whether the swapchain needs to be recreated after presenting.
             */
            [[nodiscard]]
            bool CompositeToFramebufferAndPresent ();

            /**
             * @brief Execute staged composition operation to a given image.
             * The image is guaranteed to be in a layout suitable for sampling after the composite.
             * 
             * If timeout is not zero, this method will be synchronous and will wait for the copy
             * command to be completed until timed out.
             * If the method is called in an asynchronous way, the fence returned can be used
             * to check its completion.
             * 
             * @warning This should be the last method to be called each frame, and it will progress the internal state machine.
             * Either `CompositeToFramebufferAndPresent()` or `CompositeToImage()` must be called exactly once each frame.
             * Non-standard call-sites are likely to result in deadlocks and vulkan device losses.
             */
            vk::Fence CompositeToImage (vk::Image image, uint64_t timeout = std::numeric_limits<uint64_t>::max());

            SubmissionHelper & GetSubmissionHelper();
        };
    }
}

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
