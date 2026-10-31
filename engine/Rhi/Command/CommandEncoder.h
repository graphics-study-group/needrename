#ifndef ENGINE_RHI_COMMANDENCODER_INCLUDED
#define ENGINE_RHI_COMMANDENCODER_INCLUDED

#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>

namespace Engine::Rhi {
    /**
     * @brief Records commands into one command buffer as a flat stream.
     *
     * The encoder is the seam's only recording surface. It does not expose
     * command-buffer or pass vocabulary: a caller records work and, where it
     * knows data has to be made visible to later work, records a synchronization
     * mark (`Barrier`). A backend that can only synchronize at coarse boundaries
     * carries the ordering in those boundaries; a backend with explicit barriers
     * records one at each mark. Either way the caller states *where* ordering is
     * needed and never *how* it is achieved.
     *
     * The encoder is constructed over a command buffer that some other layer
     * owns and hands out, which is why it holds an opaque handle rather than
     * creating one.
     *
     * @invariant An encoder records into exactly one command buffer for its
     * whole lifetime, and that command buffer is in the recording state.
     */
    class RHI_API CommandEncoder {
    public:
        /**
         * @brief Construct an encoder over an existing command buffer.
         *
         * @param handle The command buffer to record into, in the recording
         * state. The encoder does not take ownership of it.
         */
        explicit CommandEncoder(CommandBufferHandle handle);
        ~CommandEncoder();

        CommandEncoder(const CommandEncoder &) = delete;
        CommandEncoder &operator=(const CommandEncoder &) = delete;
        CommandEncoder(CommandEncoder &&) noexcept;
        CommandEncoder &operator=(CommandEncoder &&) noexcept;

        /**
         * @brief Record a synchronization mark.
         *
         * The mark says "work recorded after this point observes everything
         * recorded before it". It carries no stage or access information,
         * because that is the backend's business: a backend with explicit
         * barriers chooses the widest correct pair, and a backend that orders at
         * coarser boundaries makes the mark a no-op.
         *
         * @note The mark is meaningful only for shader-visible data. Transfers
         * and attachment transitions are the recording layer's own concern.
         */
        void Barrier();

        /// @brief Get the command buffer this encoder records into.
        CommandBufferHandle GetHandle() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMMANDENCODER_INCLUDED
