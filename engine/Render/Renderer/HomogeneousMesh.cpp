#include "HomogeneousMesh.h"
#include <vulkan/vulkan.hpp>

namespace Engine {
    HomogeneousMesh::HomogeneousMesh(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    HomogeneousMesh::~HomogeneousMesh() {
    }

    void HomogeneousMesh::Prepare() {
        assert(m_positions.size() % 3 == 0);
        const uint32_t new_vertex_count = m_positions.size() / 3;
        const uint64_t buffer_size = new_vertex_count * SINGLE_VERTEX_BUFFER_SIZE;

        auto system = this->m_system.lock();
        auto device = system->getDevice();

        if (m_vertex_count != new_vertex_count) {
            m_vertex_count = new_vertex_count;
            SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "(Re-)Allocating buffer and memory for %u vertices.", m_vertex_count);
            // Create buffer
            vk::BufferCreateInfo info{
                vk::BufferCreateFlags{0}, 
                buffer_size,
                vk::BufferUsageFlagBits::eVertexBuffer,
                vk::SharingMode::eExclusive
            };
            m_buffer = device.createBufferUnique(info);

            // Allocate memory
            vk::MemoryRequirements requirements = device.getBufferMemoryRequirements(m_buffer.get());
            uint32_t memory_index = system->FindPhysicalMemory(
                requirements.memoryTypeBits, 
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            m_memory = device.allocateMemoryUnique({std::max(buffer_size, requirements.size), memory_index});
            device.bindBufferMemory(m_buffer.get(), m_memory.get(), 0);
        }

        // Copy host data to mapped memory
        std::byte * data = reinterpret_cast<std::byte*>(device.mapMemory(m_memory.get(), 0, buffer_size));
        uint64_t offset = 0;
        // Position
        std::memcpy(&data[offset], m_positions.data(), m_positions.size() * sizeof(float));
        offset += m_positions.size() * sizeof(float);
        // Color
        std::memcpy(&data[offset], m_colors.data(), m_colors.size() * sizeof(float));
        offset += m_colors.size() * sizeof(float);
        assert(offset == buffer_size);
        device.unmapMemory(m_memory.get());
    }

    vk::PipelineVertexInputStateCreateInfo HomogeneousMesh::GetVertexInputState() {
        return vk::PipelineVertexInputStateCreateInfo{
            vk::PipelineVertexInputStateCreateFlags{0}, bindings, attributes
        };
    }

    uint32_t HomogeneousMesh::GetVertexCount() const {
        return m_vertex_count;
    }

    std::pair <std::array<vk::Buffer, HomogeneousMesh::BINDING_COUNT>, std::array<vk::DeviceSize, HomogeneousMesh::BINDING_COUNT>> 
    HomogeneousMesh::GetBindingInfo() const {
        assert(this->m_buffer);
        std::array<vk::Buffer, BINDING_COUNT> buffer {this->m_buffer.get(), this->m_buffer.get()};
        std::array<vk::DeviceSize, BINDING_COUNT> binding_offset {0, m_positions.size() * sizeof(float)};
        return std::make_pair(buffer, binding_offset);
    }
}
