#include "Rhi/Buffer/DeviceBuffer.h"

#include "Rhi/Device/AllocatorState.h"

namespace Engine::Rhi {
    DeviceBuffer::DeviceBuffer(BufferAllocation &&alloc, size_t size, const std::string &name) :
        m_size(size), allocation(std::move(alloc)), m_name(name) {
    }

    DeviceBuffer DeviceBuffer::Create(
        const Rhi::AllocatorState &allocator, BufferType type, size_t size, const std::string &name
    ) {
        return DeviceBuffer(allocator.AllocateBuffer(type, size, name), size);
    }

    std::unique_ptr<DeviceBuffer> DeviceBuffer::CreateUnique(
        const Rhi::AllocatorState &allocator, BufferType type, size_t size, const std::string &name
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

    void DeviceBuffer::ReallocateStorage(const Rhi::AllocatorState &allocator, size_t bytes) {
        // The right operand is evaluated before the assignment destroys the previous allocation,
        // so the replacement exists before the old storage is released.
        allocation = allocator.AllocateBuffer(allocation.GetMemoryType(), bytes, m_name);
        m_size = bytes;
    }
} // namespace Engine::Rhi
