#include "HomogeneousMesh.h"
#include <vulkan/vulkan.hpp>

namespace Engine {
    HomogeneousMesh::HomogeneousMesh(std::weak_ptr<RenderSystem> system) : m_system(system), m_buffer(system) {
    }

    HomogeneousMesh::~HomogeneousMesh() {
    }

    void HomogeneousMesh::Prepare() {
        assert(m_positions.size() % 3 == 0);
        const uint32_t new_vertex_count = m_positions.size() / 3;
        const uint64_t buffer_size = new_vertex_count * SINGLE_VERTEX_BUFFER_SIZE_WITH_INDEX;

        if (m_vertex_count != new_vertex_count) {
            m_updated = true;
            m_vertex_count = new_vertex_count;
            SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "(Re-)Allocating buffer and memory for %u vertices.", m_vertex_count);
            m_buffer.Create(Buffer::BufferType::Vertex, buffer_size);
        }
    }

    bool HomogeneousMesh::NeedCommitment() {
        bool need = m_updated;
        m_updated = false;
        return need;
    }

    Buffer HomogeneousMesh::WriteToStagingBuffer() const {
        assert(m_positions.size() % 3 == 0);
        assert(m_positions.size() / 3 == GetVertexCount());
        const uint64_t buffer_size = GetVertexCount() * SINGLE_VERTEX_BUFFER_SIZE_WITH_INDEX;

        Buffer buffer(m_system);
        buffer.Create(Buffer::BufferType::Staging, buffer_size);

        auto device = m_system.lock()->getDevice();
        std::byte * data = buffer.Map();
        WriteToMemory(data);
        buffer.Unmap();

        // Copy eilision here.
        return buffer;
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
        assert(this->m_buffer.GetBuffer());
        uint64_t total_size = GetVertexCount() * SINGLE_VERTEX_BUFFER_SIZE_WITHOUT_INDEX;
        return std::make_pair(m_buffer.GetBuffer(), total_size);
    }

    uint32_t HomogeneousMesh::GetVertexCount() const {
        assert(m_vertex_count == m_indices.size());
        return m_vertex_count;
    }

    const Buffer & HomogeneousMesh::GetBuffer() const {
        return m_buffer;
    }

    std::pair <std::array<vk::Buffer, HomogeneousMesh::BINDING_COUNT>, std::array<vk::DeviceSize, HomogeneousMesh::BINDING_COUNT>> 
    HomogeneousMesh::GetBindingInfo() const {
        assert(this->m_buffer.GetBuffer());
        std::array<vk::Buffer, BINDING_COUNT> buffer {this->m_buffer.GetBuffer(), this->m_buffer.GetBuffer()};
        std::array<vk::DeviceSize, BINDING_COUNT> binding_offset {0, m_positions.size() * sizeof(float)};
        return std::make_pair(buffer, binding_offset);
    }
}
