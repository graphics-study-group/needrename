#include "Buffer.h"
#include "Render/RenderSystem.h"

namespace Engine
{
    Buffer::Buffer(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    void Buffer::Create(BufferType type, size_t size) {
        auto system = m_system.lock();
        auto device = system->getDevice();
        auto property = GetMemoryProperty(type);
        auto usage =  GetBufferUsage(type);

        m_size = size;

        vk::BufferCreateInfo buffer_info {
            vk::BufferCreateFlags{0},
            m_size,
            usage,
            vk::SharingMode::eExclusive
        };
        m_buffer = device.createBufferUnique(buffer_info);

        vk::MemoryRequirements requirements = device.getBufferMemoryRequirements(m_buffer.get());
        uint32_t memory_index = system->FindPhysicalMemory(
            requirements.memoryTypeBits, 
            property
        );
        m_memory = device.allocateMemoryUnique({std::max(m_size, requirements.size), memory_index});
        device.bindBufferMemory(m_buffer.get(), m_memory.get(), m_offset);
    }

    vk::Buffer Buffer::GetBuffer() const {
        return m_buffer.get();
    }

    vk::DeviceMemory Buffer::GetMemory() const {
        return m_memory.get();
    }

    std::byte* Buffer::Map() const {
        auto device = m_system.lock()->getDevice();
        return reinterpret_cast<std::byte*>(device.mapMemory(m_memory.get(), m_offset, m_size));
    }

    void Buffer::Unmap() const {
        auto device = m_system.lock()->getDevice();
        device.unmapMemory(m_memory.get());
    }

    vk::MemoryPropertyFlags Buffer::GetMemoryProperty(BufferType type) {
        switch (type) {
            case BufferType::Staging:
                return vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible;
            case BufferType::Vertex:
                return vk::MemoryPropertyFlagBits::eDeviceLocal;
            case BufferType::Uniform:
                return vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible;
        }
        return vk::MemoryPropertyFlags();
    }

    vk::BufferUsageFlags Buffer::GetBufferUsage(BufferType type) {
        switch (type) {
            case BufferType::Staging:
                return vk::BufferUsageFlagBits::eTransferSrc;
            case BufferType::Vertex:
                return vk::BufferUsageFlagBits::eTransferDst 
                    | vk::BufferUsageFlagBits::eIndexBuffer 
                    | vk::BufferUsageFlagBits::eVertexBuffer;
            case BufferType::Uniform:
                return vk::BufferUsageFlagBits::eUniformBuffer;
        }
        return vk::BufferUsageFlags();
    }

    
} // namespace Engine

