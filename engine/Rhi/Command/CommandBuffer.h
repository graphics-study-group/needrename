#ifndef ENGINE_RHI_COMMAND_BUFFER_INCLUDED
#define ENGINE_RHI_COMMAND_BUFFER_INCLUDED

#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Engine::Rhi {

    class DeviceContext;

    /// @brief Which pool a command buffer is allocated from.
    enum class CommandBufferPool : uint8_t {
        /// @brief Reused every frame, reset before recording.
        Graphics,
        /// @brief Recorded once and submitted once.
        GraphicsOneTime
    };

    /**
     * @brief One recorded list of commands, and its lifetime.
     *
     * A command buffer is where recording lands and what a submission names. It
     * owns itself, so a frame that keeps FRAMES_IN_FLIGHT of them does not have to
     * pair allocations with frees.
     *
     * @note Movable but non-copyable.
     */
    class RHI_API CommandBuffer {
    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

        CommandBuffer(std::unique_ptr<impl> p) noexcept;

    public:
        /**
         * @brief Allocate command buffers from one of the device's pools.
         *
         * @param device_context The device that owns them.
         * @param pool Which pool they come from.
         * @param count How many.
         */
        static std::vector<CommandBuffer> Create(
            const DeviceContext &device_context, CommandBufferPool pool, uint32_t count
        );

        ~CommandBuffer();

        CommandBuffer(const CommandBuffer &) = delete;
        CommandBuffer &operator=(const CommandBuffer &) = delete;
        CommandBuffer(CommandBuffer &&) noexcept;
        CommandBuffer &operator=(CommandBuffer &&) noexcept;

        /// @brief The portable handle, for `CommandEncoder` and `Submission`.
        CommandBufferHandle GetHandle() const noexcept;

        /// @brief Start recording, discarding whatever was recorded before.
        void Begin();

        /// @brief Finish recording.
        void End();

        /// @brief Discard the recording so it can start again.
        void Reset();
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMMAND_BUFFER_INCLUDED
