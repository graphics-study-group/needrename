#include "Buffer.h"
#include "Render/RenderSystem.h"

namespace Engine
{
    Buffer::Buffer(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    void Buffer::Create(BufferType type, size_t size, const std::string & name) {
        auto & allocator_state = m_system.lock()->GetAllocatorState();
        m_allocated_memory = std::make_unique<AllocatedMemory>(allocator_state.AllocateBuffer(type, size, name));
        m_size = size;
    }

    vk::Buffer Buffer::GetBuffer() const {
        return m_allocated_memory->GetBuffer();
    }

    size_t Buffer::GetSize() const {
        return m_size;
    }

    std::byte* Buffer::Map() const {
        return m_allocated_memory->MapMemory();
    }

    void Buffer::Flush(size_t offset, size_t size) const {
        m_allocated_memory->FlushMemory(offset, size);
    }

    void Buffer::Invalidate(size_t offset, size_t size) const {
        m_allocated_memory->InvalidateMemory(offset, size);
    }

    void Buffer::Unmap() const {
        m_allocated_memory->UnmapMemory();
    }
} // namespace Engine

