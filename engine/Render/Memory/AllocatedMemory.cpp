#include "AllocatedMemory.h"

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace Engine {

    struct AllocatedMemory::impl {
        std::variant<vk::Image, vk::Buffer> m_vk_handle;
        VmaAllocation m_allocation;
        VmaAllocator m_allocator;
        std::byte *m_mapped_memory{nullptr};
    };

    void AllocatedMemory::ClearAndInvalidate() {
        if (pimpl->m_allocator == nullptr) return;

        assert(pimpl->m_allocation);

        if (pimpl->m_mapped_memory) {
            this->UnmapMemory();
        }

        switch (pimpl->m_vk_handle.index()) {
        case 0:
            vmaDestroyImage(pimpl->m_allocator, std::get<0>(pimpl->m_vk_handle), pimpl->m_allocation);
            break;
        case 1:
            vmaDestroyBuffer(pimpl->m_allocator, std::get<1>(pimpl->m_vk_handle), pimpl->m_allocation);
            break;
        }
    }

    AllocatedMemory::AllocatedMemory(vk::Image image, VmaAllocation allocation, VmaAllocator allocator) :
        pimpl(std::make_unique<impl>(image, allocation, allocator)) {
    }
    AllocatedMemory::AllocatedMemory(vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator) :
        pimpl(std::make_unique<impl>(buffer, allocation, allocator)) {
    }

    AllocatedMemory::~AllocatedMemory() {
        this->ClearAndInvalidate();
    }

    AllocatedMemory::AllocatedMemory(AllocatedMemory &&other) {
        pimpl = std::make_unique<impl>(other.pimpl->m_vk_handle, other.pimpl->m_allocation, other.pimpl->m_allocator);

        // Reset other
        other.pimpl->m_vk_handle = {};
        other.pimpl->m_allocation = nullptr;
        other.pimpl->m_allocator = nullptr;
    }
    AllocatedMemory &AllocatedMemory::operator=(AllocatedMemory &&other) {
        if (&other != this) {
            assert(pimpl);
            this->ClearAndInvalidate();

            pimpl->m_vk_handle = other.pimpl->m_vk_handle;
            pimpl->m_allocation = other.pimpl->m_allocation;
            pimpl->m_allocator = other.pimpl->m_allocator;

            // Reset other
            other.pimpl->m_vk_handle = {};
            other.pimpl->m_allocation = nullptr;
            other.pimpl->m_allocator = nullptr;
        }
        return *this;
    }
    vk::Buffer AllocatedMemory::GetBuffer() const {
        assert(pimpl->m_vk_handle.index() == 1 && "Getting buffer handle from other memory handle type.");
        return static_cast<vk::Buffer>(std::get<1>(pimpl->m_vk_handle));
    }
    vk::Image AllocatedMemory::GetImage() const {
        assert(pimpl->m_vk_handle.index() == 0 && "Getting image handle from other memory handle type.");
        return static_cast<vk::Image>(std::get<0>(pimpl->m_vk_handle));
    }
    std::byte *AllocatedMemory::MapMemory() {
        assert(pimpl->m_allocator && pimpl->m_allocation && "Invalild allocator or allocation.");
        assert(pimpl->m_vk_handle.index() == 1 && "Invaild mapping of non-buffer data to host memory.");
        if (pimpl->m_mapped_memory) {
            // assert(m_allocation->GetMappedData() == m_mapped_memory && "Inconsistent mapped memory pointer.");
            return pimpl->m_mapped_memory;
        }
        void *ptr{nullptr};
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaMapMemory(pimpl->m_allocator, pimpl->m_allocation, &ptr)), "Cannot map memory."
        );
        pimpl->m_mapped_memory = reinterpret_cast<std::byte *>(ptr);
        return pimpl->m_mapped_memory;
    }
    void AllocatedMemory::FlushMemory(size_t offset, size_t size) {
        if (size == 0) {
            size = VK_WHOLE_SIZE;
        }
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaFlushAllocation(pimpl->m_allocator, pimpl->m_allocation, offset, size)),
            "Failed to flush mapped memory."
        );
    }
    void AllocatedMemory::InvalidateMemory(size_t offset, size_t size) {
        if (size == 0) {
            size = VK_WHOLE_SIZE;
        }
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaInvalidateAllocation(pimpl->m_allocator, pimpl->m_allocation, offset, size)),
            "Failed to invalidate mapped memory."
        );
    }
    void AllocatedMemory::UnmapMemory() {
        assert(pimpl->m_allocator && pimpl->m_allocation && "Invalild allocator or allocation.");
        vmaUnmapMemory(pimpl->m_allocator, pimpl->m_allocation);
        pimpl->m_mapped_memory = nullptr;
    }
} // namespace Engine
