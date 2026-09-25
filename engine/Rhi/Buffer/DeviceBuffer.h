#ifndef ENGINE_RHI_DEVICEBUFFER_INCLUDED
#define ENGINE_RHI_DEVICEBUFFER_INCLUDED

#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>
#include <string>

namespace vk {
    class Buffer;
}

namespace Engine::Rhi {

    class BufferAllocation;
    class AllocatorState;

    /**
     *  @brief A buffer with allocated memory, which could be directly used by the device.
     * Call named constructors to get an instance.
     *
     * @note Movable but non-copyable.
     */
    class RHI_API DeviceBuffer {
    protected:
        DeviceBuffer(BufferAllocation &&alloc, size_t size, const std::string &name = "");

    public:
        virtual ~DeviceBuffer() = default;

        DeviceBuffer(const DeviceBuffer &) = delete;
        void operator=(const DeviceBuffer &) = delete;

        DeviceBuffer(DeviceBuffer &&) noexcept = default;
        DeviceBuffer &operator=(DeviceBuffer &&) noexcept = default;

        /**
         * @brief Create a buffer.
         */
        static DeviceBuffer Create(
            const Rhi::AllocatorState &allocator, BufferType type, size_t size, const std::string &name = ""
        );
        /**
         * @brief Create a buffer with host side details on heap,
         * and return a uniquely owning pointer to the buffer.
         *
         * Cast the `unique_ptr` to `shared_ptr` if necessary.
         */
        static std::unique_ptr<DeviceBuffer> CreateUnique(
            const Rhi::AllocatorState &allocator, BufferType type, size_t size, const std::string &name = ""
        );

        /// @brief Get the underlying Vulkan buffer object.
        vk::Buffer GetBuffer() const;

        /// @brief Get the actual size of the buffer.
        size_t GetSize() const;

        /**
         * @brief Get the pointer to the mapped address in the
         * virtual memory of this process.
         *
         * The pointer is automatically unmapped on deconstruction.
         * You don't need to match `Unmap()` manually before
         * cleaning up.
         */
        std::byte *GetVMAddress();

        /**
         * @brief Flush the memory write to be visible on device.
         *
         * Generally you don't need to manually call this member, as memories
         * that need to be flushed are usually coherent.
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
         * need to be invalidated are usually coherent.
         *
         * @param offset Offset of the region to be invalidated
         *
         * @param size Size of the region to be invalidated, or whole region if 0.
         */
        void Invalidate(size_t offset = 0, size_t size = 0) const;

        /**
         * @brief Get the memory type assigned to the buffer when allocated.
         */
        BufferType GetType() const noexcept;

    protected:
        /**
         * @brief Replace the buffer's storage in place, keeping the buffer object
         * at the same address.
         *
         * The replacement is allocated before the previous storage is released,
         * and the released allocation takes the allocator's usual retirement
         * path: with a retirement facility installed it is parked under the
         * submission epoch instead of being freed; without one it is destroyed
         * immediately.
         *
         * The buffer's contents are discarded and `GetBuffer()` /
         * `GetVMAddress()` obtained before the call are invalidated. Any
         * reference to the buffer object itself survives.
         *
         * @param allocator The allocator the replacement storage is allocated from.
         * @param bytes Size of the replacement storage.
         */
        void ReallocateStorage(const Rhi::AllocatorState &allocator, size_t bytes);

        size_t m_size{0ULL};
        BufferAllocation allocation;
        std::string m_name = "";
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_DEVICEBUFFER_INCLUDED
