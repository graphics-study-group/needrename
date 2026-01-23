#ifndef RENDER_MEMORY_COMPUTEBUFFER_INCLUDED
#define RENDER_MEMORY_COMPUTEBUFFER_INCLUDED

#include "DeviceBuffer.h"

#include <span>

namespace Engine {
    /**
     * @brief A buffer dedicated for compute shader use (i.e. storage buffer).
     */
    class ComputeBuffer : public DeviceBuffer {
        ComputeBuffer(BufferAllocation && alloc, size_t size);
    public:
        /**
         * @brief Create a new compute buffer.
         * 
         * @param allow_cpu_access Enables CPU access. Guarantees that `GetVMAddress()` can be used.
         * @param as_readonly_buffer Allows it to be used as uniform buffer.
         * @param as_vertex_buffer Allows it to be used as vertex and index buffer.
         * @param as_indirect_draw_buffer Allows it to be used as indirect draw command buffer.
         */
        static std::unique_ptr<ComputeBuffer> CreateUnique(
            const RenderSystemState::AllocatorState & allocator,
            size_t size,
            bool allow_cpu_access,
            bool as_readonly_buffer,
            bool as_vertex_buffer,
            bool as_indirect_draw_buffer,
            const std::string &name = ""
        );
    };

    template <class T>
    class ComputeBufferTyped : public ComputeBuffer {
    public:
        /**
         * @brief Create a new compute buffer.
         * 
         * @param allow_cpu_access Enables CPU access. Guarantees that `GetVMAddress()` can be used.
         * @param as_readonly_buffer Allows it to be used as uniform buffer.
         * @param as_vertex_buffer Allows it to be used as vertex and index buffer.
         * @param as_indirect_draw_buffer Allows it to be used as indirect draw command buffer.
         */
        template <class U>
        static std::unique_ptr<ComputeBufferTyped<U>> CreateUniqueTyped(
            const RenderSystemState::AllocatorState & allocator,
            size_t count,
            bool allow_cpu_access,
            bool as_readonly_buffer,
            bool as_vertex_buffer,
            bool as_indirect_draw_buffer,
            const std::string &name = ""
        ) {
            return ComputeBuffer::CreateUnique(
                allocator,
                count * sizeof(T),
                allow_cpu_access,
                as_readonly_buffer,
                as_vertex_buffer,
                as_indirect_draw_buffer,
                name
            );
        }

        std::span<T> GetVMAddressTyped() {
            return std::span<T>(reinterpret_cast<T *>(GetVMAddress()), GetSize());
        }
    };
};

#endif // RENDER_MEMORY_COMPUTEBUFFER_INCLUDED
