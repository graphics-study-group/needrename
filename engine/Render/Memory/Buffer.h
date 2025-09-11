#ifndef RENDER_MEMORY_BUFFER_INCLUDED
#define RENDER_MEMORY_BUFFER_INCLUDED

#include <memory>

#include "Render/RenderSystem/AllocatorState.h"

namespace Engine {

    class RenderSystem;

    /// @brief A buffer with allocated memory, which could be on device or host.
    class Buffer {
    protected:
        Buffer(BufferAllocation && alloc, size_t size);

    public:
        using BufferType = RenderSystemState::AllocatorState::BufferType;

        Buffer (const Buffer &) = delete;
        void operator= (const Buffer &) = delete;

        Buffer (Buffer &&) noexcept = default;

        /// @brief Create a buffer, and perform allocation if needed.
        /// @param type
        /// @param size
        static Buffer Create(RenderSystem & system, BufferType type, size_t size, const std::string &name = "");
        static std::unique_ptr<Buffer> CreateUnique(RenderSystem & system, BufferType type, size_t size, const std::string &name = "");

        vk::Buffer GetBuffer() const;

        size_t GetSize() const;

        /**
         * @brief Get the pointer to the mapped address in the 
         * virtual memory of this process.
         * 
         * The pointer is
         * automatically unmapped on deconstruction.
         * You don't need to match `Unmap()` manually before
         * cleaning up.
         */
        std::byte * GetVMAddress();

        /**
         * @brief Flush the memory write to be visible on device.
         * 
         * Generally you don't
         * need to manually call this member, as memories that
         * need to be flushed are usually coherent.
         *
         * @param offset Offset of the region to be flushed
         * @param size Size of the region to be flushed,
         * or whole region if 0.
         */
        void Flush(size_t offset = 0, size_t size = 0) const;

        /**
         * @brief Invalidate the memory so that device write are visible on host.
         * 
         *
         * Generally you don't need to manually call this member, as memories that
         * need to be invalidated are
         * usually coherent.
         * 
         * @param offset Offset of the region to be invalidated
         *
         * @param size Size of the region to be invalidated, or whole region if 0.
         */
        void Invalidate(size_t offset = 0, size_t size = 0) const;

    protected:
        size_t m_size{0ULL};
        BufferAllocation allocation;
    };
} // namespace Engine

#endif // RENDER_MEMORY_BUFFER_INCLUDED
