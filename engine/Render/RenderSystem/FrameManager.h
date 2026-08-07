#ifndef RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED

#include <functional>
// May be safe to include here as this header is not included in other headers.
#include <vulkan/vulkan.hpp>

#include "Rhi/MemoryAccessTypes.h"

namespace Engine {
    namespace Rhi {
        class DeviceBuffer;
        class SubmissionHelper;
    } // namespace Rhi
    class RenderSystem;
    class Texture;
    class CommandBuffer;
    class RenderTargetTexture;
    class IPresentProvider;

    namespace RenderSystemState {
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
            void Create(IPresentProvider &present_provider);

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
             * @brief Begin recording the main command buffer of the current
             * frame in flight.
             *
             * The main command buffer lifecycle (begin/end/submit) belongs to
             * FrameManager: begin here, record render/physics commands, then
             * call `SubmitFrame()` which ends and submits it.
             *
             * @warning Must be called between `StartFrame()` and `SubmitFrame()`,
             * once each frame.
             */
            CommandBuffer BeginMainCommandBuffer();

            /**
             * @brief Get the raw main command buffer handle of the current
             * frame in flight.
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
             * @brief Frame completion: the single per-frame submit + present.
             *
             * Ends the main command buffer, records the copy command buffer via
             * `IPresentProvider::PrepareCopy` (nullptr when headless), submits
             * ONE `vkQueueSubmit2` batch containing both the main render CB and
             * the copy CB (in-order execution, zero signals between them;
             * barriers handle layout), then presents waiting on the frame
             * completion credential.
             *
             * The batch unconditionally signals `timeline@4` + the
             * command-executed fence, so the frame sync chain closes even in
             * headless mode (no deadlock).
             *
             * @param present_texture Final render target to present.
             * @param last_access Access mode of `present_texture` in its last
             *                    pass (used to derive the copy source barrier).
             * @return True if the swapchain needs to be recreated.
             */
            [[nodiscard]]
            bool SubmitFrame(const RenderTargetTexture &present_texture, Rhi::MemoryAccessTypeImageBits last_access);

            /// @brief Get the submission helper.
            Rhi::SubmissionHelper &GetSubmissionHelper();

            /// @brief Get the current frame semaphore.
            const FrameSemaphore &GetFrameSemaphore() const noexcept;

            /**
             * @brief Function type of callback of readback operations.
             *
             * Data retrieved from the device is stored in the device buffer.
             * This buffer can be mapped to the host VM for reading.
             */
            using ReadbackCallback = std::function<void(std::unique_ptr<Rhi::DeviceBuffer>)>;
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
            bool RegisterReadbackCallback(const Rhi::DeviceBuffer &buffer, ReadbackCallback cb);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_FRAMEMANAGER_INCLUDED