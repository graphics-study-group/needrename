#include "Rhi/Buffer/ComputeBuffer.h"

#include "Rhi/Device/AllocatorState.h"

namespace Engine::Rhi {
    namespace {
        BufferType ComputeBufferType(
            bool allow_cpu_access, bool as_readonly_buffer, bool as_vertex_buffer, bool as_indirect_draw_buffer
        ) {
            BufferType type{BufferTypeBits::ShaderWrite, BufferTypeBits::CopyFrom, BufferTypeBits::CopyTo};
            if (allow_cpu_access) type.Set(BufferTypeBits::HostRandomAccess);
            if (as_readonly_buffer) type.Set(BufferTypeBits::ShaderReadOnly);
            if (as_vertex_buffer) type.Set(BufferTypeBits::Vertex), type.Set(BufferTypeBits::Index);
            if (as_indirect_draw_buffer) type.Set(BufferTypeBits::IndirectDrawCommand);
            return type;
        }
    } // namespace

    ComputeBuffer::ComputeBuffer(BufferAllocation &&alloc, size_t size) : DeviceBuffer(std::move(alloc), size) {
    }

    std::unique_ptr<ComputeBuffer> ComputeBuffer::CreateUnique(
        const Rhi::AllocatorState &allocator,
        size_t size,
        bool allow_cpu_access,
        bool as_readonly_buffer,
        bool as_vertex_buffer,
        bool as_indirect_draw_buffer,
        const std::string &name
    ) {
        const BufferType type =
            ComputeBufferType(allow_cpu_access, as_readonly_buffer, as_vertex_buffer, as_indirect_draw_buffer);
        return std::unique_ptr<ComputeBuffer>(new ComputeBuffer(allocator.AllocateBuffer(type, size, name), size));
    }

    std::shared_ptr<ComputeBuffer> ComputeBuffer::CreateShared(
        const Rhi::AllocatorState &allocator,
        size_t size,
        bool allow_cpu_access,
        bool as_readonly_buffer,
        bool as_vertex_buffer,
        bool as_indirect_draw_buffer,
        const std::string &name
    ) {
        const BufferType type =
            ComputeBufferType(allow_cpu_access, as_readonly_buffer, as_vertex_buffer, as_indirect_draw_buffer);
        return std::shared_ptr<ComputeBuffer>(new ComputeBuffer(allocator.AllocateBuffer(type, size, name), size));
    }
} // namespace Engine::Rhi
