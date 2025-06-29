#ifndef RENDER_MEMORY_INDEXEDBUFFER_INCLUDED
#define RENDER_MEMORY_INDEXEDBUFFER_INCLUDED

#include "Render/Memory/Buffer.h"

namespace Engine {

    /**
     * @brief Buffer of a large trunk of memory.
     * The buffer is separated into multiple slices of the same size, and
     * each slice can be indexed for a slice pointer.
     * 
     * The slice pointer is guaranteed to be aligned accordingly, making the
     * buffer suitable to use for dynamic buffer descriptors.
     */
    class IndexedBuffer : private Buffer {
        struct impl;
        std::unique_ptr <impl> pimpl;

    public:
        using Buffer::BufferType;

        IndexedBuffer (RenderSystem & system);
        virtual ~IndexedBuffer ();

        /**
         * @brief Create a buffer that is large enough to hold aligned slices.
         * 
         * @param type Type of the buffer. Currently only uniform is supported.
         * @param slice_size Actual size of each slice, ignoring alignment requirement.
         * @param slice_alignment Alignment requirement of each slice.
         * @param name Name of the buffer.
         */
        void Create (
            BufferType type, 
            size_t slice_size, 
            size_t slice_alignment, 
            uint32_t slices, 
            const std::string & name = ""
        );

        using Buffer::GetBuffer;
        using Buffer::GetSize;

        /**
         * @brief Get actual slice size ignoring padding for alignments.
         */
        size_t GetSliceSize() const noexcept;

        /**
         * @brief Get slice size with alignment
         */
        size_t GetAlignedSliceSize() const noexcept;

        /**
         * @brief Get the pointer to the slice.
         * 
         * The slice pointer is aligned to guarantee that
         * `GetSlicePtr(slice) % slice_alignment == 0`.
         * 
         * The pointer is already offset, and you do not need to offset it again.
         */
        void * GetSlicePtr(uint32_t slice) const noexcept;

        /**
         * @brief Get the offset of the slice to the base pointer mapped.
         * 
         * The slice pointer is aligned to guarantee that
         * `GetSliceOffset(slice) % slice_alignment == 0`.
         */
        std::ptrdiff_t GetSliceOffset(uint32_t slice) const noexcept;
        
        /**
         * @brief Flush the slice write to be visible on device.
         * 
         * Generally you don't need to manually call this member, as memories that
         * need to be flushed are usually coherent.
         */
        void FlushSlice(uint32_t slice) const;

        /**
         * @brief Invalidate the slice so that device write are visible on host.
         * 
         * Generally you don't need to manually call this member, as memories that
         * need to be invalidated are usually coherent.
         */
        void InvalidateSlice(uint32_t slice);
    };

    /**
     * @brief A typed adaptor of `IndexedBuffer`.
     */
    template <class T> requires std::is_standard_layout_v<T>
    class TypedIndexedBuffer : public IndexedBuffer {
    public:
        TypedIndexedBuffer(RenderSystem & sys) : IndexedBuffer(sys) {};

        /**
         * @brief Create the buffer object and perform allocation accordingly.
         * 
         * Effectively calls `IndexedBuffer::Create(type, sizeof(T), slice_alignment, slices, name)`.
         */
        void Create(
            BufferType type,
            size_t slice_alignment,
            uint32_t slices,
            const std::string & name = ""
        ) {
            IndexedBuffer::Create(type, sizeof(T), slice_alignment, slices, name);
        }

        /**
         * @brief Get a typed pointer to the slice.
         */
        T* GetSlicePtrTyped (uint32_t slice) const noexcept {
            return reinterpret_cast<T *>(GetSlicePtr(slice));
        }
    };
}

#endif // RENDER_MEMORY_INDEXEDBUFFER_INCLUDED
