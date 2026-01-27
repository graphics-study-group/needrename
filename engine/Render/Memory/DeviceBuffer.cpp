#include "DeviceBuffer.h"
#include "Render/RenderSystem.h"

namespace Engine {
    DeviceBuffer::DeviceBuffer(
        BufferAllocation && alloc,
        size_t size
    ) : m_size(size), allocation(std::move(alloc)){
    }

    DeviceBuffer DeviceBuffer::Create(const RenderSystemState::AllocatorState & allocator, BufferType type, size_t size, const std::string &name) {
        return DeviceBuffer(allocator.AllocateBuffer(type, size, name), size);
    }

    std::unique_ptr<DeviceBuffer> DeviceBuffer::CreateUnique(
        const RenderSystemState::AllocatorState &allocator, BufferType type, size_t size, const std::string &name
    ) {
        return std::unique_ptr<DeviceBuffer>(new DeviceBuffer(allocator.AllocateBuffer(type, size, name), size));
    }

    vk::Buffer DeviceBuffer::GetBuffer() const {
        return allocation.GetBuffer();
    }

    size_t DeviceBuffer::GetSize() const {
        return m_size;
    }

    std::byte *DeviceBuffer::GetVMAddress() {
        return allocation.GetVMAddress();
    }

    void DeviceBuffer::Flush(size_t offset, size_t size) const {
        allocation.FlushMemory(offset, size);
    }

    void DeviceBuffer::Invalidate(size_t offset, size_t size) const {
        allocation.InvalidateMemory(offset, size);
    }
    BufferType DeviceBuffer::GetType() const noexcept {
        return allocation.GetMemoryType();
    }
} // namespace Engine
