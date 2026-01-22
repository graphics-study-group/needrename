#include "IndexedBuffer.h"

namespace Engine {

    struct IndexedBuffer::impl {
        size_t slice_size;
        size_t slice_alignment;
        uint32_t slices;

        size_t aligned_slice_size;
        std::byte * base_ptr;
    };

    IndexedBuffer::IndexedBuffer(
        BufferAllocation && alloc, 
        size_t size,
        size_t slice_size,
        size_t slice_alignment,
        uint32_t slices,
        size_t aligned_slice_size
    ) : DeviceBuffer(std::move(alloc), size), 
        pimpl(std::make_unique<IndexedBuffer::impl>(
            slice_size, 
            slice_alignment, 
            slices, 
            aligned_slice_size, 
            nullptr
        )) {
        pimpl->base_ptr = DeviceBuffer::GetVMAddress();
    }

    IndexedBuffer::IndexedBuffer(IndexedBuffer &&) noexcept = default;

    IndexedBuffer &IndexedBuffer::operator=(IndexedBuffer &&) noexcept = default;

    IndexedBuffer::~IndexedBuffer() = default;

    IndexedBuffer IndexedBuffer::Create(
        const RenderSystemState::AllocatorState & allocator,
        BufferType type,
        size_t slice_size,
        size_t slice_alignment,
        uint32_t slices,
        const std::string &name
    ) {
        size_t aligned_size =
            slice_alignment ? ((slice_size + slice_alignment - 1) & ~(slice_alignment - 1)) : slice_size;

        return IndexedBuffer(
            allocator.AllocateBuffer(type, aligned_size * slices, name), 
            aligned_size * slices,
            slice_size,
            slice_alignment,
            slices,
            aligned_size
        );
    }

    std::unique_ptr<IndexedBuffer> IndexedBuffer::CreateUnique(
        const RenderSystemState::AllocatorState & allocator,
        BufferType type,
        size_t slice_size,
        size_t slice_alignment,
        uint32_t slices,
        const std::string &name
    ) {
        size_t aligned_size =
            slice_alignment ? ((slice_size + slice_alignment - 1) & ~(slice_alignment - 1)) : slice_size;

        return std::unique_ptr<IndexedBuffer>(new IndexedBuffer(
                allocator.AllocateBuffer(type, aligned_size * slices, name), 
                aligned_size * slices,
                slice_size,
                slice_alignment,
                slices,
                aligned_size
            ));
    }

    size_t IndexedBuffer::GetSliceSize() const noexcept {
        return pimpl->slice_size;
    }

    size_t IndexedBuffer::GetAlignedSliceSize() const noexcept {
        return pimpl->aligned_slice_size;
    }

    void *IndexedBuffer::GetSlicePtr(uint32_t slice) const noexcept {
        assert(slice < pimpl->slices);
        return pimpl->base_ptr + pimpl->aligned_slice_size * slice;
    }

    std::ptrdiff_t IndexedBuffer::GetSliceOffset(uint32_t slice) const noexcept {
        return pimpl->aligned_slice_size * slice;
    }

    void IndexedBuffer::FlushSlice(uint32_t slice) const {
        DeviceBuffer::Flush(GetSliceOffset(slice), pimpl->aligned_slice_size);
    }

    void IndexedBuffer::InvalidateSlice(uint32_t slice) {
        DeviceBuffer::Invalidate(GetSliceOffset(slice), pimpl->aligned_slice_size);
    }
} // namespace Engine
