#include "AllocatedMemory.h"

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace Engine {

    struct AllocatedMemory::impl {
        VmaAllocation m_allocation;
        VmaAllocator m_allocator;
    };

    AllocatedMemory::AllocatedMemory(VmaAllocation allocation, VmaAllocator allocator) :
        pimpl(std::make_unique<impl>(allocation, allocator)) {
    }
    AllocatedMemory::~AllocatedMemory() {
        // vmaFreeMemory(GetAllocator(), GetAllocation());
    }

    AllocatedMemory::AllocatedMemory(AllocatedMemory &&other) {
        pimpl = std::make_unique<impl>(other.pimpl->m_allocation, other.pimpl->m_allocator);

        // Reset other
        other.pimpl->m_allocation = nullptr;
        other.pimpl->m_allocator = nullptr;
    }
    AllocatedMemory &AllocatedMemory::operator=(AllocatedMemory &&other) {
        if (&other != this) {
            assert(pimpl);

            pimpl->m_allocation = other.pimpl->m_allocation;
            pimpl->m_allocator = other.pimpl->m_allocator;

            // Reset other
            other.pimpl->m_allocation = nullptr;
            other.pimpl->m_allocator = nullptr;
        }
        return *this;
    }
    const VmaAllocation &AllocatedMemory::GetAllocation() const noexcept {
        return pimpl->m_allocation;
    }
    const VmaAllocator &AllocatedMemory::GetAllocator() const noexcept {
        return pimpl->m_allocator;
    }

    struct ImageAllocation::impl {
        vk::Image image;
    };

    ImageAllocation::ImageAllocation(
        vk::Image image, 
        VmaAllocation allocation, 
        VmaAllocator allocator
    ) : AllocatedMemory(allocation, allocator), pimpl(std::make_unique<impl>(image)) {
    }
    ImageAllocation::~ImageAllocation() {
        vmaDestroyImage(GetAllocator(), pimpl->image, GetAllocation());
    }
    
    const vk::Image & ImageAllocation::GetImage() const noexcept {
        return pimpl->image;
    }
    struct BufferAllocation::impl {
        vk::Buffer buffer;
        std::byte * mapped_ptr;
    };
    BufferAllocation::BufferAllocation(
        vk::Buffer buffer, 
        VmaAllocation allocation, 
        VmaAllocator allocator) : AllocatedMemory(allocation, allocator), pimpl(std::make_unique<impl>(buffer, nullptr)) {
    }
    BufferAllocation::~BufferAllocation() {
        assert(pimpl->buffer);
        vmaDestroyBuffer(GetAllocator(), pimpl->buffer, GetAllocation());
    }
    BufferAllocation::BufferAllocation(
        BufferAllocation &&other
    ) : AllocatedMemory(std::move(other)), pimpl(std::make_unique<impl>(other.pimpl->buffer, other.pimpl->mapped_ptr)) {
        other.pimpl->buffer = nullptr;
        other.pimpl->mapped_ptr = nullptr;
    }
    const vk::Buffer &BufferAllocation::GetBuffer() const noexcept {
        return pimpl->buffer;
    }
    std::byte *BufferAllocation::GetVMAddress() {
        if (pimpl->mapped_ptr) {
            return pimpl->mapped_ptr;
        }
        void *ptr{nullptr};
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaMapMemory(GetAllocator(), GetAllocation(), &ptr)), "Cannot map memory."
        );
        pimpl->mapped_ptr = reinterpret_cast<std::byte *>(ptr);
        return pimpl->mapped_ptr;
    }
    void BufferAllocation::FlushMemory(size_t offset, size_t size) const {
        if (size == 0) {
            size = VK_WHOLE_SIZE;
        }
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaFlushAllocation(GetAllocator(), GetAllocation(), offset, size)),
            "Failed to flush mapped memory."
        );
    }
    void BufferAllocation::InvalidateMemory(size_t offset, size_t size) const {
        if (size == 0) {
            size = VK_WHOLE_SIZE;
        }
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaInvalidateAllocation(GetAllocator(), GetAllocation(), offset, size)),
            "Failed to invalidate mapped memory."
        );
    }
} // namespace Engine
