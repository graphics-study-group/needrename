#ifndef RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
#define RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED

#include <vk_mem_alloc.h>

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

        std::byte * MapMemory();
        void UnmapMemory();
    };
}

#endif // RENDER_MEMORY_ALLOCATEDMEMORY_INCLUDED
