#include "ComputeBuffer.h"

namespace Engine {

    ComputeBuffer::ComputeBuffer(BufferAllocation && alloc, size_t size) : DeviceBuffer(std::move(alloc), size){

    }

    std::unique_ptr<ComputeBuffer> ComputeBuffer::CreateUnique(
        const RenderSystemState::AllocatorState &allocator,
        size_t size,
        bool allow_cpu_access,
        bool as_readonly_buffer,
        bool as_vertex_buffer,
        bool as_indirect_draw_buffer,
        const std::string &name
    ) {
        BufferType type{BufferTypeBits::ShaderWrite, BufferTypeBits::CopyFrom, BufferTypeBits::CopyTo};
        if (allow_cpu_access) type.Set(BufferTypeBits::HostRandomAccess);
        if (as_readonly_buffer) type.Set(BufferTypeBits::ShaderReadOnly);
        if (as_vertex_buffer) type.Set(BufferTypeBits::Vertex), type.Set(BufferTypeBits::Index);
        if (as_indirect_draw_buffer) type.Set(BufferTypeBits::IndirectDrawCommand);

        return std::unique_ptr<ComputeBuffer>(
            new ComputeBuffer(allocator.AllocateBuffer(type, size, name), size)
        );
    }
}
