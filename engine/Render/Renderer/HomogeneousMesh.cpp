#include "HomogeneousMesh.h"
#include <vulkan/vulkan.hpp>

namespace Engine {
    HomogeneousMesh::HomogeneousMesh(std::weak_ptr<RenderSystem> system) : m_system(system), m_buffer(system) {
    }

    HomogeneousMesh::~HomogeneousMesh() {
    }

    void HomogeneousMesh::Prepare() {
        assert(m_positions.size() % 3 == 0);
        const uint32_t new_vertex_count = GetVertexCount();
        const uint64_t buffer_size = GetExpectedBufferSize();

        if (m_allocated_buffer_size != buffer_size) {
            m_updated = true;
            SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "(Re-)Allocating buffer and memory for %u vertices.", new_vertex_count);
            m_buffer.Create(Buffer::BufferType::Vertex, buffer_size);
            m_allocated_buffer_size = buffer_size;
        }
    }

    bool HomogeneousMesh::NeedCommitment() {
        bool need = m_updated;
        m_updated = false;
        return need;
    }

    Buffer HomogeneousMesh::CreateStagingBuffer() const {
        assert(m_positions.size() % 3 == 0);
        const uint64_t buffer_size = GetExpectedBufferSize();

        Buffer buffer(m_system);
        buffer.Create(Buffer::BufferType::Staging, buffer_size);

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

    void HomogeneousMesh::SetPositions(std::vector<float> positions) {
        assert(positions.size() % 3 == 0);
        m_positions = positions;
        m_updated = true;
    }

    void HomogeneousMesh::SetColors(std::vector<float> colors) {
        assert(colors.size() % 3 == 0);
        m_colors = colors;
        m_updated = true;
    }

    void HomogeneousMesh::SetIndices(std::vector<uint32_t> indices) {
        m_indices = indices;
        m_updated = true;
    }

    std::pair<vk::Buffer, vk::DeviceSize> HomogeneousMesh::GetIndexInfo() const {
        assert(this->m_buffer.GetBuffer());
        uint64_t total_size = GetVertexCount() * SINGLE_VERTEX_BUFFER_SIZE_WITHOUT_INDEX;
        return std::make_pair(m_buffer.GetBuffer(), total_size);
    }

    uint32_t HomogeneousMesh::GetVertexIndexCount() const {
        return m_indices.size();
    }

    uint32_t HomogeneousMesh::GetVertexCount() const {
        // TODO: clear up assumptions on vertex data.
        assert(m_positions.size() % 3 == 0);
        assert(m_colors.size() % 3 == 0);
        assert(m_positions.size() / 3 == m_colors.size() / 3);
        return m_positions.size() / 3;
    }

    uint64_t HomogeneousMesh::GetExpectedBufferSize() const {
        return GetVertexIndexCount() * sizeof(uint32_t) + GetVertexCount() * SINGLE_VERTEX_BUFFER_SIZE_WITHOUT_INDEX;
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
