#ifndef RENDER_MEMORY_BUFFER_INCLUDED
#define RENDER_MEMORY_BUFFER_INCLUDED

#include <memory>

#include "Render/Memory/MemoryTypes.h"
#include "Render/RenderSystem/AllocatorState.h"

namespace Engine {

    class RenderSystem;

    /**
     *  @brief A buffer with allocated memory, which could be directly used by the device.
     * Call named constructors to get an instance.
     * 
     * @note Movable but non-copyable.
     */
    class DeviceBuffer {
    protected:
        DeviceBuffer(BufferAllocation && alloc, size_t size);

    public:

        virtual ~DeviceBuffer() = default;

        DeviceBuffer (const DeviceBuffer &) = delete;
        void operator= (const DeviceBuffer &) = delete;

        DeviceBuffer (DeviceBuffer &&) noexcept = default;
        DeviceBuffer & operator= (DeviceBuffer &&) noexcept = default;

        /**
         * @brief Create a buffer.
         */
        static DeviceBuffer Create(
            const RenderSystemState::AllocatorState & allocator, 
            BufferType type, 
            size_t size, 
            const std::string &name = ""
        );
        /**
         * @brief Create a buffer with host side details on heap,
         * and return a uniquely owning pointer to the buffer.
         * 
         * Cast the `unique_ptr` to `shared_ptr` if necessary.
         */
        static std::unique_ptr<DeviceBuffer> CreateUnique(
            const RenderSystemState::AllocatorState & allocator, 
            BufferType type, 
            size_t size, const 
            std::string &name = ""
        );

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
