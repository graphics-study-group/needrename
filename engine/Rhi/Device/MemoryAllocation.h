#ifndef ENGINE_RHI_MEMORYALLOCATION_INCLUDED
#define ENGINE_RHI_MEMORYALLOCATION_INCLUDED

#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <cassert>
#include <memory>

namespace Engine::Rhi {
    class EpochTracker;
    class VulkanIdentity;

    /**
     * @brief A piece of memory allocated by Vulkan via Vulkan Memory Allocator.
     *
     * Memory allocation is a complex topic in Vulkan programming. In short,
     * this class represents a typeless memory allocation in Vulkan, on device
     * or on host, over which an image or a buffer can be created on.
     *
     * This class should not be visible to users. Use `ImageAllocation` for
     * images and `BufferAllocation` for buffers.
     *
     * Use `Engine::Rhi::AllocatorState` to allocate memory.
     *
     * @invariant This class, once created, is guaranteed to hold a vaild memory
     * allocation until moved or destructed.
     */
    class VmaMemoryAllocation {
        void *m_allocation;
        void *m_allocator;

    public:
        VmaMemoryAllocation() : m_allocation(nullptr), m_allocator(nullptr) {
        }

        /// @brief Create an allocation, called from `Engine::Rhi::AllocatorState`.
        VmaMemoryAllocation(void *allocation, void *allocator) : m_allocation(allocation), m_allocator(allocator) {
        }
        ~VmaMemoryAllocation() {
        }

        VmaMemoryAllocation(const VmaMemoryAllocation &) = delete;
        VmaMemoryAllocation &operator=(const VmaMemoryAllocation &) = delete;

        /// @brief Construct an allocation from another allocation.
        VmaMemoryAllocation(VmaMemoryAllocation &&other) = default;
        /// @brief Construct an allocation from another allocation.
        VmaMemoryAllocation &operator=(VmaMemoryAllocation &&other) = default;

        void *GetAllocation() const noexcept {
            assert(m_allocation);
            return m_allocation;
        }
        void *GetAllocator() const noexcept {
            assert(m_allocator);
            return m_allocator;
        }
    };

    /**
     * @brief A piece of memory allocation for images.
     *
     * Use `Engine::Rhi::AllocatorState` to perform memory
     * allocation.
     *
     * @invariant This class, once created, is guaranteed to hold a vaild image
     * allocation until moved or destructed.
     */
    class RHI_API ImageAllocation : private VmaMemoryAllocation {
        struct impl;
        std::unique_ptr<impl> pimpl;

        /// @brief Destory the resource stored inside the pimpl pointer, and reset the pointer.
        void Destory() noexcept;

    public:
        /// @brief Create an image allocation, called from `Engine::Rhi::AllocatorState`.
        ImageAllocation(ImageHandle image, void *allocation, void *allocator, ImageMemoryType type);

        ~ImageAllocation();

        ImageAllocation(const ImageAllocation &) = delete;
        void operator=(const ImageAllocation &) = delete;

        /**
         * @brief Construct an allocation from another allocation.
         *
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        ImageAllocation(ImageAllocation &&other) noexcept;

        /**
         * @brief Acquire an allocation from another allocation.
         *
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        ImageAllocation &operator=(ImageAllocation &&other) noexcept;

        /// @brief Get the underlying Vulkan image object.
        ImageHandle GetImage() const noexcept;
        /// @brief Query the memory type specified on creation.
        ImageMemoryType GetMemoryType() const noexcept;

        /**
         * @brief Get this allocation's never-reused identity.
         *
         * The identity is issued when the allocation is created and never
         * issued again: a replacement allocation bears a new one.
         */
        VulkanIdentity GetIdentity() const noexcept;
    };

    /**
     * @brief A piece of memory allocation for buffer.
     *
     * This class represents a low-level memory interface for buffer memory.
     * Users should prefer to use `Engine::Rhi::DeviceBuffer` or related classes
     * to manipulate data.
     *
     * Use `Engine::Rhi::AllocatorState` to perform memory
     * allocation.
     *
     * @invariant This class, once created, is guaranteed to hold a vaild buffer
     * allocation until moved or destructed.
     *
     * @note When an epoch tracker is installed on the allocator that created
     * this allocation, destruction hands the allocation to that tracker instead
     * of freeing device memory. See `Engine::Rhi::EpochTracker`.
     */
    class RHI_API BufferAllocation : private VmaMemoryAllocation {
        struct impl;
        std::unique_ptr<impl> pimpl;

        /// @brief Destory the resource stored inside the pimpl pointer, and reset the pointer.
        void Destroy() noexcept;

    public:
        /**
         * @brief Create a buffer allocation, called from `Engine::Rhi::AllocatorState`.
         *
         * @param retire_sink Optional tracker that takes ownership on
         * destruction. When null, the allocation frees its device memory on
         * destruction.
         */
        BufferAllocation(
            BufferHandle buffer,
            void *allocation,
            void *allocator,
            BufferType type,
            EpochTracker *retire_sink = nullptr
        );
        ~BufferAllocation();

        BufferAllocation(const BufferAllocation &) = delete;
        BufferAllocation &operator=(const BufferAllocation &) = delete;

        /**
         * @brief Construct an allocation from another allocation.
         *
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        BufferAllocation(BufferAllocation &&other) noexcept;

        /**
         * @brief Acquire an allocation from another allocation.
         *
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        BufferAllocation &operator=(BufferAllocation &&other) noexcept;

        /// @brief Get the underlying Vulkan buffer object.
        BufferHandle GetBuffer() const noexcept;

        /**
         * @brief Get this allocation's never-reused identity.
         *
         * The identity is issued when the allocation is created and never
         * issued again: replacing a buffer's storage creates a new allocation
         * with a new identity, which is what distinguishes the buffer from its
         * own predecessor.
         */
        VulkanIdentity GetIdentity() const noexcept;

        /**
         * @brief Detach the retire sink, so that destruction frees device memory.
         *
         * The tracker takes ownership by moving the allocation into its own
         * storage and MUST call this first. Otherwise destroying the allocation
         * would hand it straight back to the same tracker, forever.
         */
        void ClearRetireSink() noexcept;

        /**
         * @brief Get the memory address on the host virtual memory
         * that maps to the buffer.
         *
         * @exception Throws if the buffer cannot be mapped to host VM.
         * @return pointer to the content of the buffer
         */
        std::byte *GetVMAddress();

        /**
         * @brief Make a readback buffer's contents addressable, before a caller reads them.
         *
         * On a backend whose mapping is asynchronous, the map has to be requested and waited
         * for somewhere that is not already dispatching the device's events. The frame pump
         * calls this for every buffer it is about to hand to a readback callback, which is the
         * one place that is true.
         *
         * A backend whose mapping is synchronous - or which keeps the window open - has nothing
         * to do here.
         */
        void PrepareReadback();

        /**
         * @brief Give a readback buffer back to the device, after its contents were read.
         *
         * A buffer that is still mapped cannot be the destination of a copy: an implementation
         * rejects the submission rather than the map, and the failure lands frames away from the
         * read that caused it. The frame pump calls this once the callback has returned, so a
         * pooled readback buffer goes back to the device in a state it can write.
         */
        void ReleaseReadback();

        /**
         * @brief Flush the memory so that host writes are visible on the device
         *
         * @exception std::runtime_error Rethrows all exception if the underlying Vulkan call fails
         */
        void FlushMemory(size_t offset = 0, size_t size = 0) const;

        /**
         * @brief Invalidate the memory so that device writes are visible on
         * the host.
         *
         * @exception std::runtime_error Rethrows all exception if the underlying Vulkan call fails
         */
        void InvalidateMemory(size_t offset = 0, size_t size = 0) const;

        /// @brief Query the memory type specified on creation.
        BufferType GetMemoryType() const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_MEMORYALLOCATION_INCLUDED


