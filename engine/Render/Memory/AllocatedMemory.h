#ifndef RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
#define RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED

#include <variant>

class VmaAllocation_T;
class VmaAllocator_T;
class VmaAllocationInfo;
typedef VmaAllocation_T* VmaAllocation;
typedef VmaAllocator_T* VmaAllocator;

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

        const VmaAllocation & GetAllocation() const noexcept;
        const VmaAllocator & GetAllocator() const noexcept;
        VmaAllocationInfo QueryAllocationInfo() const noexcept;
    };

    class ImageAllocation : private AllocatedMemory {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        ImageAllocation(vk::Image image, VmaAllocation allocation, VmaAllocator allocator);
        ~ImageAllocation();

        ImageAllocation(const ImageAllocation &) = delete;
        void operator=(const ImageAllocation &) = delete;

        ImageAllocation(ImageAllocation && other) noexcept;
        ImageAllocation & operator= (ImageAllocation && other) noexcept;

        const vk::Image & GetImage() const noexcept;
    };

    class BufferAllocation : private AllocatedMemory {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        BufferAllocation(vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator);
        ~BufferAllocation();

        BufferAllocation(const BufferAllocation &) = delete;
        void operator=(const BufferAllocation &) = delete;

        BufferAllocation(BufferAllocation && other) noexcept;
        BufferAllocation & operator= (BufferAllocation && other) noexcept;

        const vk::Buffer & GetBuffer() const noexcept;

        std::byte * GetVMAddress();

        void FlushMemory(size_t offset = 0, size_t size = 0) const;
        void InvalidateMemory(size_t offset = 0, size_t size = 0) const;
    };
} // namespace Engine

#endif // RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
