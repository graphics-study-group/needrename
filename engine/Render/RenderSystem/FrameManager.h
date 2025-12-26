#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

// May be safe to include here as this header is not included in other headers.
#include <vulkan/vulkan.hpp>

namespace Engine {
    class RenderSystem;
    class GraphicsCommandBuffer;
    class GraphicsContext;
    class ComputeContext;

    namespace RenderSystemState {
        class SubmissionHelper;
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

            [[nodiscard]]
            bool PresentToFramebuffer(
                vk::Image image,
                vk::Extent2D extentSrc,
                vk::Offset2D offsetSrc = {0, 0},
                vk::Filter filter = vk::Filter::eLinear
            );

            SubmissionHelper &GetSubmissionHelper();
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
