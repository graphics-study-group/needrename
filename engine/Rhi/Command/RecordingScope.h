#ifndef ENGINE_RHI_RECORDINGSCOPE_INCLUDED
#define ENGINE_RHI_RECORDINGSCOPE_INCLUDED

#include "Rhi/rhi_export.h"

#include <memory>

namespace Engine::Rhi {
    class CommandEncoder;
    class DeviceContext;

    /**
     * @brief One recording that owns its submission.
     *
     * A caller that already has a frame's command buffer asks that buffer for its
     * encoder. A caller that has no such object — a physics test that wants to run
     * one dispatch, a one-shot upload — opens a scope instead: the scope owns a
     * command buffer and the submission that carries it, and hands out the encoder
     * to record into.
     *
     * This is the only way to obtain an encoder without already owning a command
     * buffer, and it is what keeps a raw command-buffer handle from crossing a
     * module boundary.
     *
     * @invariant The encoder is valid from construction until `Submit`, and the
     * scope waits for the device before it returns from that call.
     */
    class RHI_API RecordingScope {
    public:
        /**
         * @brief Open a recording on the device's one-time submission path.
         *
         * @param context The device the work is recorded against. The scope does
         * not outlive it.
         */
        explicit RecordingScope(DeviceContext &context);
        ~RecordingScope();

        RecordingScope(const RecordingScope &) = delete;
        RecordingScope &operator=(const RecordingScope &) = delete;
        RecordingScope(RecordingScope &&) = delete;
        RecordingScope &operator=(RecordingScope &&) = delete;

        /// @brief The encoder to record into.
        CommandEncoder &Encoder() noexcept;

        /**
         * @brief End the recording, submit it, and wait for the device.
         *
         * Calling this twice is a no-op, so a caller may submit explicitly and let
         * the destructor run afterwards.
         */
        void Submit();

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_RECORDINGSCOPE_INCLUDED
