#include "IndexedBuffer.h"

namespace Engine {

    struct IndexedBuffer::impl {
        size_t slice_size;
        size_t slice_alignment;
        uint32_t slices;

        size_t aligned_slice_size;
        void *base_ptr;
    };

    IndexedBuffer::IndexedBuffer(RenderSystem &system) :
        Buffer(system), pimpl(std::make_unique<IndexedBuffer::impl>()) {
    }

    IndexedBuffer::~IndexedBuffer() = default;

    void IndexedBuffer::Create(
        BufferType type, size_t slice_size, size_t slice_alignment, uint32_t slices, const std::string &name) {
        assert(type == BufferType::Uniform && "Currently only uniform buffer can be indexed.");
        std::tie(pimpl->slice_size, pimpl->slice_alignment, pimpl->slices) = {slice_size, slice_alignment, slices};

        size_t aligned_size =
            slice_alignment ? ((slice_size + slice_alignment - 1) & ~(slice_alignment - 1)) : slice_size;
        pimpl->aligned_slice_size = aligned_size;

        Buffer::Create(type, aligned_size * slices, name);
        pimpl->base_ptr = Buffer::Map();
        assert(pimpl->base_ptr);
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
