#include "Rhi/Buffer/ComputeBuffer.h"

#include "Rhi/Device/AllocatorState.h"

#include <algorithm>
#include <limits>

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

    ComputeBuffer::ComputeBuffer(BufferAllocation &&alloc, size_t size, const std::string &name) :
        DeviceBuffer(std::move(alloc), size, name) {
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
        return std::unique_ptr<ComputeBuffer>(
            new ComputeBuffer(allocator.AllocateBuffer(type, size, name), size, name)
        );
    }

    void ComputeBuffer::Reallocate(const Rhi::AllocatorState &allocator, size_t bytes) {
        ReallocateStorage(allocator, bytes);
    }

    void ComputeBuffer::EnsureCapacity(const Rhi::AllocatorState &allocator, size_t bytes) {
        const size_t current = GetSize();
        if (bytes <= current) return;
        // Geometric, never shrinking: capacity tracks capacity steps rather than
        // every value change, so an oscillating workload stops reallocating once
        // it has reached its high-water mark.
        const size_t doubled = (current > std::numeric_limits<size_t>::max() / 2u) ? current : current * 2u;
        ReallocateStorage(allocator, std::max(bytes, doubled));
    }

    void EnsureComputeBuffer(
        std::unique_ptr<ComputeBuffer> &buffer,
        const Rhi::AllocatorState &allocator,
        size_t bytes,
        bool allow_cpu_access,
        const std::string &name
    ) {
        if (!buffer) {
            buffer = ComputeBuffer::CreateUnique(allocator, bytes, allow_cpu_access, false, false, false, name);
            return;
        }
        buffer->EnsureCapacity(allocator, bytes);
    }
} // namespace Engine::Rhi
