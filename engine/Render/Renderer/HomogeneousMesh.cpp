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
        const uint64_t buffer_size = new_vertex_count * SINGLE_VERTEX_BUFFER_SIZE_WITH_INDEX;

        auto system = this->m_system.lock();
        auto device = system->getDevice();

        if (m_vertex_count != new_vertex_count) {
            m_updated = true;
            m_vertex_count = new_vertex_count;
            SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "(Re-)Allocating buffer and memory for %u vertices.", m_vertex_count);
            // Create buffer
            vk::BufferCreateInfo info{
                vk::BufferCreateFlags{0}, 
                buffer_size,
                vk::BufferUsageFlagBits::eVertexBuffer | 
                vk::BufferUsageFlagBits::eIndexBuffer | 
                vk::BufferUsageFlagBits::eTransferDst,
                vk::SharingMode::eExclusive
            };
            m_buffer = device.createBufferUnique(info);

            // Allocate memory
            vk::MemoryRequirements requirements = device.getBufferMemoryRequirements(m_buffer.get());
            uint32_t memory_index = system->FindPhysicalMemory(
                requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            m_memory = device.allocateMemoryUnique({std::max(buffer_size, requirements.size), memory_index});
            device.bindBufferMemory(m_buffer.get(), m_memory.get(), 0);
        }
    }

    bool HomogeneousMesh::NeedCommitment() {
        bool need = m_updated;
        m_updated = false;
        return need;
    }

    std::pair<vk::UniqueBuffer, vk::UniqueDeviceMemory> HomogeneousMesh::WriteToStagingBuffer() const {
        assert(m_positions.size() % 3 == 0);
        assert(m_positions.size() / 3 == GetVertexCount());
        const uint64_t buffer_size = GetVertexCount() * SINGLE_VERTEX_BUFFER_SIZE_WITH_INDEX;

        auto system = this->m_system.lock();
        auto device = system->getDevice();

        vk::UniqueBuffer staging_buffer;
        vk::UniqueDeviceMemory staging_memory;

        vk::BufferCreateInfo info{
            vk::BufferCreateFlags{0}, 
            buffer_size,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::SharingMode::eExclusive
        };
        staging_buffer = device.createBufferUnique(info);

        vk::MemoryRequirements requirements = device.getBufferMemoryRequirements(m_buffer.get());
        uint32_t memory_index = system->FindPhysicalMemory(
            requirements.memoryTypeBits, 
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        staging_memory = device.allocateMemoryUnique({std::max(buffer_size, requirements.size), memory_index});
        device.bindBufferMemory(staging_buffer.get(), staging_memory.get(), 0);

        std::byte * data = reinterpret_cast<std::byte*>(device.mapMemory(staging_memory.get(), 0, buffer_size));
        WriteToMemory(data);
        device.unmapMemory(staging_memory.get());

        return std::make_pair(std::move(staging_buffer), std::move(staging_memory));
    }

    void HomogeneousMesh::WriteToMemory(std::byte* pointer) const {
        uint64_t offset = 0;
        // Position
        std::memcpy(&pointer[offset], m_positions.data(), m_positions.size() * sizeof(float));
        offset += m_positions.size() * sizeof(float);
        // Color
        std::memcpy(&pointer[offset], m_colors.data(), m_colors.size() * sizeof(float));
        offset += m_colors.size() * sizeof(float);

#ifndef NDEBUG
        assert(m_positions.size() % 3 == 0);
        const uint32_t vertex_count = m_positions.size() / 3;
        const uint64_t buffer_size = vertex_count * SINGLE_VERTEX_BUFFER_SIZE_WITHOUT_INDEX;
        assert(offset == buffer_size);
#endif

        // Index
        std::memcpy(&pointer[offset], m_indices.data(), m_indices.size() * sizeof(uint32_t));
    }

    vk::PipelineVertexInputStateCreateInfo HomogeneousMesh::GetVertexInputState() {
        return vk::PipelineVertexInputStateCreateInfo{
            vk::PipelineVertexInputStateCreateFlags{0}, bindings, attributes
        };
    }

    std::pair<vk::Buffer, vk::DeviceSize> HomogeneousMesh::GetIndexInfo() const {
        assert(this->m_buffer);
        uint64_t total_size = GetVertexCount() * SINGLE_VERTEX_BUFFER_SIZE_WITHOUT_INDEX;
        return std::make_pair(m_buffer.get(), total_size);
    }

    uint32_t HomogeneousMesh::GetVertexCount() const {
        assert(m_vertex_count == m_indices.size());
        return m_vertex_count;
    }

    const vk::Buffer & HomogeneousMesh::GetBuffer() const {
        return m_buffer.get();
    }

    std::pair <std::array<vk::Buffer, HomogeneousMesh::BINDING_COUNT>, std::array<vk::DeviceSize, HomogeneousMesh::BINDING_COUNT>> 
    HomogeneousMesh::GetBindingInfo() const {
        assert(this->m_buffer);
        std::array<vk::Buffer, BINDING_COUNT> buffer {this->m_buffer.get(), this->m_buffer.get()};
        std::array<vk::DeviceSize, BINDING_COUNT> binding_offset {0, m_positions.size() * sizeof(float)};
        return std::make_pair(buffer, binding_offset);
    }
}
