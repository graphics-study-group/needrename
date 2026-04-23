#ifndef RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
#define RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED

#include "MemoryTypes.h"
#include <variant>

class VmaAllocation_T;
class VmaAllocator_T;
class VmaAllocationInfo;
typedef VmaAllocation_T *VmaAllocation;
typedef VmaAllocator_T *VmaAllocator;

namespace vk {
    class Image;
    class Buffer;
} // namespace vk

namespace Engine {
    /**
     * @brief A piece of contiguous memory allocated by Vulkan.
     */
    class AllocatedMemory {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        AllocatedMemory(VmaAllocation allocation, VmaAllocator allocator);
        virtual ~AllocatedMemory();

        AllocatedMemory(const AllocatedMemory &) = delete;
        void operator=(const AllocatedMemory &) = delete;

        AllocatedMemory(AllocatedMemory &&other) noexcept;
        AllocatedMemory &operator=(AllocatedMemory &&other) noexcept;

        const VmaAllocation &GetAllocation() const noexcept;
        const VmaAllocator &GetAllocator() const noexcept;
        VmaAllocationInfo QueryAllocationInfo() const noexcept;
    };

    /// @brief A piece of memory allocation for images.
    class ImageAllocation : private AllocatedMemory {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        ImageAllocation(vk::Image image, VmaAllocation allocation, VmaAllocator allocator, ImageMemoryType type);
        ~ImageAllocation();

        ImageAllocation(const ImageAllocation &) = delete;
        void operator=(const ImageAllocation &) = delete;

        ImageAllocation(ImageAllocation &&other) noexcept;
        ImageAllocation &operator=(ImageAllocation &&other) noexcept;

        /// @brief Get the underlying Vulkan image object.
        const vk::Image &GetImage() const noexcept;
        /// @brief Query the memory type specified on creation.
        ImageMemoryType GetMemoryType() const noexcept;
    };

    /// @brief A piece of memory allocation for buffers.
    class BufferAllocation : private AllocatedMemory {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        BufferAllocation(vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator, BufferType type);
        ~BufferAllocation();

        BufferAllocation(const BufferAllocation &) = delete;
        void operator=(const BufferAllocation &) = delete;

        BufferAllocation(BufferAllocation &&other) noexcept;
        BufferAllocation &operator=(BufferAllocation &&other) noexcept;

        /// @brief Get the underlying Vulkan buffer object.
        const vk::Buffer &GetBuffer() const noexcept;
        /**
         * @brief Get the memory address on the host virtual memory
         * that maps to the buffer.
         *
         * @exception Throws if the buffer cannot be mapped to host VM.
         * @return pointer to the content of the buffer
         */
        std::byte *GetVMAddress();

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
} // namespace Engine

#endif // RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
