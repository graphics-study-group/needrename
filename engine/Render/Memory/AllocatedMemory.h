#ifndef RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
#define RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED

#include <vk_mem_alloc.h>
#include <variant>

namespace Engine {
    class AllocatedMemory {
        std::variant <vk::Image, vk::Buffer> m_vk_handle;
        VmaAllocation m_allocation;
        VmaAllocator m_allocator;

        std::byte * m_mapped_memory {nullptr};

        void ClearAndInvalidate();

    public:
        AllocatedMemory(vk::Image image, VmaAllocation allocation, VmaAllocator allocator);
        AllocatedMemory(vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator);
        ~AllocatedMemory();

        AllocatedMemory (const AllocatedMemory &) = delete;
        void operator = (const AllocatedMemory &) = delete;

        AllocatedMemory (AllocatedMemory && other);
        AllocatedMemory & operator = (AllocatedMemory && other);

        vk::Buffer GetBuffer() const;
        vk::Image GetImage() const;

        /**
         * @brief Map the memory to a host pointer.
         * 
         * The pointer is cached in the class and automatically unmapped
         * on deconstruction. You don't need to match `UnmapMemory()` manually
         * before cleaning up.
         */
        std::byte * MapMemory();

        /**
         * @brief Flush the memory write to be visible on device.
         * 
         * Generally you don't need to manually call this member, as memories that
         * need to be flushed are usually coherent.
         */
        void FlushMemory (size_t offset = 0, size_t size = 0);
    
        /**
         * @brief Invalidate the memory so that device write are visible on host.
         * 
         * Generally you don't need to manually call this member, as memories that
         * need to be invalidated are usually coherent.
         */
        void InvalidateMemory (size_t offset = 0, size_t size = 0);

        /**
         * @brief Unmap the host pointer and reset the cached pointer.
         * 
         * Generally you don't need to manually call this member, as clean up is
         * automatically done with RAII mechanism.
         */
        void UnmapMemory();
    };
}

#endif // RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
