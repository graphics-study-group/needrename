#include "AllocatedMemory.h"

namespace Engine {
    void AllocatedMemory::ClearAndInvalidate()
    {
        if (m_allocator == nullptr) return;

        assert(m_allocation);
        switch (this->m_vk_handle.index()) {
        case 0:
            vmaDestroyImage(m_allocator, std::get<0>(m_vk_handle), m_allocation);
            break;
        case 1:
            vmaDestroyBuffer(m_allocator, std::get<1>(m_vk_handle), m_allocation);
            break;
        }
    }

    AllocatedMemory::AllocatedMemory(vk::Image image, VmaAllocation allocation, VmaAllocator allocator) : 
        m_vk_handle{image}, m_allocation{allocation}, m_allocator{allocator}
    {
    }
    AllocatedMemory::AllocatedMemory(vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator) :
        m_vk_handle{buffer}, m_allocation{allocation}, m_allocator{allocator}
    {
    }

    AllocatedMemory::~AllocatedMemory()
    {
        this->ClearAndInvalidate();
    }

    AllocatedMemory::AllocatedMemory(AllocatedMemory &&other)
    {
        this->m_vk_handle = other.m_vk_handle;
        this->m_allocation = other.m_allocation;
        this->m_allocator = other.m_allocator;

        // Reset other
        other.m_vk_handle = {};
        other.m_allocation = nullptr;
        other.m_allocator = nullptr;
    }
    AllocatedMemory &AllocatedMemory::operator=(AllocatedMemory &&other)
    {
        if (&other != this) {
            this->ClearAndInvalidate();

            this->m_vk_handle = other.m_vk_handle;
            this->m_allocation = other.m_allocation;
            this->m_allocator = other.m_allocator;

            // Reset other
            other.m_vk_handle = {};
            other.m_allocation = nullptr;
            other.m_allocator = nullptr;
        }
        return *this;
    }
    vk::Buffer AllocatedMemory::GetBuffer() const
    {
        assert(m_vk_handle.index() == 1 && "Getting buffer handle from other memory handle type.");
        return static_cast<vk::Buffer>(std::get<1>(m_vk_handle));
    }
    vk::Image AllocatedMemory::GetImage() const
    {
        assert(m_vk_handle.index() == 0 && "Getting image handle from other memory handle type.");
        return static_cast<vk::Image>(std::get<0>(m_vk_handle));
    }
    std::byte *AllocatedMemory::MapMemory()
    {
        assert(m_allocator && m_allocation && "Invalild allocator or allocation.");
        void * ptr{nullptr};
        vmaMapMemory(m_allocator, m_allocation, &ptr);
        return reinterpret_cast<std::byte*>(ptr);
    }
    void AllocatedMemory::UnmapMemory()
    {
        assert(m_allocator && m_allocation && "Invalild allocator or allocation.");
        vmaUnmapMemory(m_allocator, m_allocation);
    }
}
