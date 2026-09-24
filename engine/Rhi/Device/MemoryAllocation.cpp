#include "Rhi/Device/MemoryAllocation.h"

#include "Rhi/Submission/EpochTracker.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct ImageAllocation::impl {
        vk::Image image;
        ImageMemoryType type;
    };

    void ImageAllocation::Destory() noexcept {
        if (pimpl) {
            if (pimpl->image) {
                vmaDestroyImage(GetAllocator(), pimpl->image, GetAllocation());
            }
            pimpl.reset();
        }
    }

    ImageAllocation::ImageAllocation(
        vk::Image image, VmaAllocation allocation, VmaAllocator allocator, ImageMemoryType type
    ) : VmaMemoryAllocation(allocation, allocator), pimpl(std::make_unique<impl>(image, type)) {
    }
    ImageAllocation::~ImageAllocation() {
        Destory();
    }

    ImageAllocation::ImageAllocation(ImageAllocation &&other) noexcept :
        VmaMemoryAllocation(std::move(other)), pimpl(nullptr) {
        std::swap(pimpl, other.pimpl);
    }

    ImageAllocation &ImageAllocation::operator=(ImageAllocation &&other) noexcept {
        if (&other != this) {
            Destory();
            this->VmaMemoryAllocation::operator=(std::move(other));
            std::swap(this->pimpl, other.pimpl);
        }
        return *this;
    }

    vk::Image ImageAllocation::GetImage() const noexcept {
        assert(pimpl);
        return pimpl->image;
    }
    ImageMemoryType ImageAllocation::GetMemoryType() const noexcept {
        assert(pimpl);
        return pimpl->type;
    }

    struct BufferAllocation::impl {
        vk::Buffer buffer;
        BufferType type;
        std::byte *mapped_ptr;
        /// @brief Optional tracker that takes ownership on destruction. Cleared
        /// before the hand-off so the tracker can destroy the allocation itself.
        EpochTracker *retire_sink;
    };
    void BufferAllocation::Destroy() noexcept {
        if (pimpl && pimpl->buffer) {
            if (pimpl->retire_sink) {
                // Hand the allocation over instead of freeing it. The tracker is
                // cleared first so that the allocation it now owns destroys
                // itself for real (and cannot re-enter the tracker).
                EpochTracker *tracker = pimpl->retire_sink;
                pimpl->retire_sink = nullptr;
                tracker->Retire(std::move(*this));
                return;
            }
            if (pimpl->mapped_ptr) {
                vmaUnmapMemory(GetAllocator(), GetAllocation());
            }
            vmaDestroyBuffer(GetAllocator(), pimpl->buffer, GetAllocation());
            pimpl.reset();
        }
    }
    BufferAllocation::BufferAllocation(
        vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator, BufferType type, EpochTracker *sink
    ) : VmaMemoryAllocation(allocation, allocator), pimpl(std::make_unique<impl>(buffer, type, nullptr, sink)) {
    }
    BufferAllocation::~BufferAllocation() {
        Destroy();
    }
    BufferAllocation::BufferAllocation(BufferAllocation &&other) noexcept :
        VmaMemoryAllocation(std::move(other)), pimpl(nullptr) {
        std::swap(pimpl, other.pimpl);
    }
    BufferAllocation &BufferAllocation::operator=(BufferAllocation &&other) noexcept {
        if (&other != this) {
            this->Destroy();
            this->VmaMemoryAllocation::operator=(std::move(other));
            std::swap(this->pimpl, other.pimpl);
        }
        return *this;
    }
    vk::Buffer BufferAllocation::GetBuffer() const noexcept {
        assert(pimpl);
        return pimpl->buffer;
    }
    void BufferAllocation::ClearRetireSink() noexcept {
        if (pimpl) pimpl->retire_sink = nullptr;
    }
    std::byte *BufferAllocation::GetVMAddress() {
        assert(pimpl);

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
        assert(pimpl);

        if (size == 0) {
            size = vk::WholeSize;
        }
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaFlushAllocation(GetAllocator(), GetAllocation(), offset, size)),
            "Failed to flush mapped memory."
        );
    }
    void BufferAllocation::InvalidateMemory(size_t offset, size_t size) const {
        assert(pimpl);

        if (size == 0) {
            size = VK_WHOLE_SIZE;
        }
        vk::detail::resultCheck(
            static_cast<vk::Result>(vmaInvalidateAllocation(GetAllocator(), GetAllocation(), offset, size)),
            "Failed to invalidate mapped memory."
        );
    }
    BufferType BufferAllocation::GetMemoryType() const noexcept {
        assert(pimpl);

        return pimpl->type;
    }
} // namespace Engine::Rhi
