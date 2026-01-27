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

    AllocatedMemory::AllocatedMemory(AllocatedMemory &&other) noexcept {
        pimpl = std::make_unique<impl>(other.pimpl->m_allocation, other.pimpl->m_allocator);

        // Reset other
        other.pimpl->m_allocation = nullptr;
        other.pimpl->m_allocator = nullptr;
    }
    AllocatedMemory &AllocatedMemory::operator=(AllocatedMemory &&other) noexcept {
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

    VmaAllocationInfo AllocatedMemory::QueryAllocationInfo() const noexcept {
        VmaAllocationInfo vai;
        vmaGetAllocationInfo(GetAllocator(), GetAllocation(), &vai);
        return vai;
    }

    struct ImageAllocation::impl {
        vk::Image image;
        ImageMemoryType type;
    };

    ImageAllocation::ImageAllocation(
        vk::Image image, 
        VmaAllocation allocation, 
        VmaAllocator allocator,
        ImageMemoryType type
    ) : AllocatedMemory(allocation, allocator), pimpl(std::make_unique<impl>(image, type)) {
    }
    ImageAllocation::~ImageAllocation() {
        if (pimpl->image) vmaDestroyImage(GetAllocator(), pimpl->image, GetAllocation());
    }

    ImageAllocation::ImageAllocation(
        ImageAllocation &&other
    ) noexcept : AllocatedMemory(std::move(other)), pimpl(std::make_unique<impl>(other.pimpl->image, other.pimpl->type)) {
        other.pimpl->image = nullptr;
    }

    ImageAllocation &ImageAllocation::operator=(ImageAllocation &&other) noexcept {
        if (&other != this) {
            // XXX: Ugly, try copy-and-swap idiom.
            if (pimpl->image) vmaDestroyImage(GetAllocator(), pimpl->image, GetAllocation());
            this->AllocatedMemory::operator=(std::move(other));
            this->pimpl->image = other.pimpl->image;
            this->pimpl->type = other.pimpl->type;
            other.pimpl->image = nullptr;
        }
        return *this;
    }

    const vk::Image &ImageAllocation::GetImage() const noexcept {
        return pimpl->image;
    }
    ImageMemoryType ImageAllocation::GetMemoryType() const noexcept {
        return pimpl->type;
    }
    struct BufferAllocation::impl {
        vk::Buffer buffer;
        BufferType type;
        std::byte * mapped_ptr;
    };
    BufferAllocation::BufferAllocation(
        vk::Buffer buffer, 
        VmaAllocation allocation, 
        VmaAllocator allocator,
        BufferType type
    ) : AllocatedMemory(allocation, allocator), pimpl(std::make_unique<impl>(buffer, type, nullptr)) {
    }
    BufferAllocation::~BufferAllocation() {
        if(pimpl->buffer) {
            if (pimpl->mapped_ptr) {
                vmaUnmapMemory(GetAllocator(), GetAllocation());
            }
            vmaDestroyBuffer(GetAllocator(), pimpl->buffer, GetAllocation());
        }
    }
    BufferAllocation::BufferAllocation(
        BufferAllocation &&other
    ) noexcept : AllocatedMemory(std::move(other)), pimpl(std::make_unique<impl>(other.pimpl->buffer, other.pimpl->type, other.pimpl->mapped_ptr)) {
        other.pimpl->buffer = nullptr;
        other.pimpl->mapped_ptr = nullptr;
    }
    BufferAllocation &BufferAllocation::operator=(BufferAllocation &&other) noexcept {
        if (&other != this) {
            // XXX: Ugly, try copy-and-swap idiom.
            if(pimpl->buffer) {
                if (pimpl->mapped_ptr) {
                    vmaUnmapMemory(GetAllocator(), GetAllocation());
                }
                vmaDestroyBuffer(GetAllocator(), pimpl->buffer, GetAllocation());
            }
            this->AllocatedMemory::operator=(std::move(other));
            this->pimpl->buffer = other.pimpl->buffer;
            this->pimpl->type = other.pimpl->type;
            this->pimpl->mapped_ptr = other.pimpl->mapped_ptr;
            other.pimpl->buffer = nullptr;
            other.pimpl->mapped_ptr = nullptr;
        }
        return *this;
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
    BufferType BufferAllocation::GetMemoryType() const noexcept {
        return pimpl->type;
    }
} // namespace Engine
