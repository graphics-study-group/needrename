#ifndef RENDER_MEMORY_INDEXEDBUFFER_INCLUDED
#define RENDER_MEMORY_INDEXEDBUFFER_INCLUDED

#include "Render/Memory/Buffer.h"

namespace Engine {

    /**
     * @brief Buffer of a large trunk of memory.
     * The buffer is separated into multiple slices of the same size, and
     * each slice can be indexed for a base pointer.
     * 
     * The base pointer is guaranteed to be aligned accordingly, making the
     * buffer suitable to use for dynamic buffer descriptors.
     */
    class IndexedBuffer : private Buffer {
        struct impl;
        std::unique_ptr <impl> pimpl;

    public:
        using Buffer::BufferType;

        IndexedBuffer (RenderSystem & system);
        virtual ~IndexedBuffer ();

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
         * @brief Get the pointer to the slice.
         * 
         * The pointer is already offset, and you do not need to offset it again.
         */
        void * GetSlicePtr(uint32_t slice) const noexcept;

        /**
         * @brief Get the offset of the slice to the base pointer mapped.
         */
        std::ptrdiff_t GetSliceOffset(uint32_t slice) const noexcept;
        
        void FlushSlice(uint32_t slice) const;
        void InvalidateSlice(uint32_t slice);
    };

    /**
     * @brief A typed adaptor of `IndexedBuffer`.
     */
    template <class T> requires std::is_standard_layout_v<T>
    class TypedIndexedBuffer : public IndexedBuffer {
    public:
        TypedIndexedBuffer(RenderSystem & sys) : IndexedBuffer(sys) {};

        void Create(
            BufferType type,
            size_t slice_alignment,
            uint32_t slices,
            const std::string & name = ""
        ) {
            IndexedBuffer::Create(type, sizeof(T), slice_alignment, slices, name);
        }

        T* GetSlicePtrTyped (uint32_t slice) const noexcept {
            return reinterpret_cast<T *>(GetSlicePtr(slice));
        }
    };
}

#endif // RENDER_MEMORY_INDEXEDBUFFER_INCLUDED
