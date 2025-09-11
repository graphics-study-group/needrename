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
    ) : Buffer(std::move(alloc), size), 
        pimpl(std::make_unique<IndexedBuffer::impl>(
            slice_size, 
            slice_alignment, 
            slices, 
            aligned_slice_size, 
            nullptr
        )) {
        pimpl->base_ptr = Buffer::GetVMAddress();
    }

    IndexedBuffer::IndexedBuffer(IndexedBuffer &&) noexcept = default;

    IndexedBuffer &IndexedBuffer::operator=(IndexedBuffer &&) noexcept = default;

    IndexedBuffer::~IndexedBuffer() = default;

    IndexedBuffer IndexedBuffer::Create(
        RenderSystem & system,
        BufferType type,
        size_t slice_size,
        size_t slice_alignment,
        uint32_t slices,
        const std::string &name
    ) {
        assert(type == BufferType::Uniform && "Currently only uniform buffer can be indexed.");

        size_t aligned_size =
            slice_alignment ? ((slice_size + slice_alignment - 1) & ~(slice_alignment - 1)) : slice_size;

        auto &allocator_state = system.GetAllocatorState();
        return IndexedBuffer(
            allocator_state.AllocateBuffer(type, aligned_size * slices, name), 
            aligned_size * slices,
            slice_size,
            slice_alignment,
            slices,
            aligned_size
        );
    }

    std::unique_ptr<IndexedBuffer> IndexedBuffer::CreateUnique(
        RenderSystem &system,
        BufferType type,
        size_t slice_size,
        size_t slice_alignment,
        uint32_t slices,
        const std::string &name
    ) {
        assert(type == BufferType::Uniform && "Currently only uniform buffer can be indexed.");

        size_t aligned_size =
            slice_alignment ? ((slice_size + slice_alignment - 1) & ~(slice_alignment - 1)) : slice_size;

        auto &allocator_state = system.GetAllocatorState();
        return std::unique_ptr<IndexedBuffer>(new IndexedBuffer(
                allocator_state.AllocateBuffer(type, aligned_size * slices, name), 
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
        Buffer::Flush(GetSliceOffset(slice), pimpl->aligned_slice_size);
    }

    void IndexedBuffer::InvalidateSlice(uint32_t slice) {
        Buffer::Invalidate(GetSliceOffset(slice), pimpl->aligned_slice_size);
    }
} // namespace Engine
