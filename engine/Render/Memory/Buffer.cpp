#include "Buffer.h"
#include "Render/RenderSystem.h"

namespace Engine {
    Buffer::Buffer(
        BufferAllocation && alloc,
        size_t size
    ) : m_size(size), allocation(std::move(alloc)){
    }

    Buffer Buffer::Create(const RenderSystemState::AllocatorState & allocator, BufferType type, size_t size, const std::string &name) {
        return Buffer(allocator.AllocateBuffer(type, size, name), size);
    }

    std::unique_ptr<Buffer> Buffer::CreateUnique(
        const RenderSystemState::AllocatorState &allocator, BufferType type, size_t size, const std::string &name
    ) {
        return std::unique_ptr<Buffer>(new Buffer(allocator.AllocateBuffer(type, size, name), size));
    }

    vk::Buffer Buffer::GetBuffer() const {
        return allocation.GetBuffer();
    }

    size_t Buffer::GetSize() const {
        return m_size;
    }

    std::byte *Buffer::GetVMAddress() {
        return allocation.GetVMAddress();
    }

    void Buffer::Flush(size_t offset, size_t size) const {
        allocation.FlushMemory(offset, size);
    }

    void Buffer::Invalidate(size_t offset, size_t size) const {
        allocation.InvalidateMemory(offset, size);
    }
} // namespace Engine
