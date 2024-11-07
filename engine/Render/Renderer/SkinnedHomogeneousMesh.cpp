#include "SkinnedHomogeneousMesh.h"

namespace Engine{
    SkinnedHomogeneousMesh::SkinnedHomogeneousMesh(std::weak_ptr<RenderSystem> system) : HomogeneousMesh(system)
    {
    }

    uint64_t SkinnedHomogeneousMesh::GetExpectedBufferSize() const
    {
        return GetVertexIndexCount() * sizeof(uint32_t) 
            + GetVertexCount() * (VertexStruct::VERTEX_TOTAL_SIZE + VertexStruct::SKINNED_VERTEX_ATTRIBUTE_SIZE);
    }

    std::pair<std::vector<vk::Buffer>, std::vector<vk::DeviceSize>> SkinnedHomogeneousMesh::GetBindingInfo() const
    {
        assert(this->m_buffer.GetBuffer());
        std::vector<vk::Buffer> buffer {
            this->m_buffer.GetBuffer(), 
            this->m_buffer.GetBuffer(),
            this->m_buffer.GetBuffer()
        };
        std::vector<vk::DeviceSize> binding_offset {
            vk::DeviceSize{0}, 
            vk::DeviceSize{m_positions.size() * VertexStruct::VERTEX_POSITION_SIZE},
            vk::DeviceSize{m_positions.size() * VertexStruct::VERTEX_POSITION_SIZE + m_attributes.size() * VertexStruct::VERTEX_ATTRIBUTE_SIZE}
        };
        assert(buffer.size() == binding_offset.size());
        return std::make_pair(buffer, binding_offset);
    }

    std::pair<vk::Buffer, vk::DeviceSize> SkinnedHomogeneousMesh::GetIndexInfo() const
    {
        assert(this->m_buffer.GetBuffer());
        uint64_t total_size = GetVertexCount() * (VertexStruct::VERTEX_TOTAL_SIZE + VertexStruct::SKINNED_VERTEX_ATTRIBUTE_SIZE);
        return std::make_pair(m_buffer.GetBuffer(), total_size);
    }

    vk::PipelineVertexInputStateCreateInfo Engine::SkinnedHomogeneousMesh::GetVertexInputState()
    {
        return vk::PipelineVertexInputStateCreateInfo{
            vk::PipelineVertexInputStateCreateFlags{0}, BINDINGS, ATTRIBUTES
        };
    }

    void SkinnedHomogeneousMesh::SetBones(std::vector<VertexStruct::SkinnedVertexAttribute> attributes)
    {
        m_bones = attributes;
        m_updated = true;
    }

    void SkinnedHomogeneousMesh::WriteToMemory(std::byte *pointer) const
    {
        uint64_t offset = 0;
        // Position
        std::memcpy(&pointer[offset], m_positions.data(), m_positions.size() * VertexStruct::VERTEX_POSITION_SIZE);
        offset += m_positions.size() * VertexStruct::VERTEX_POSITION_SIZE;
        // Attributes
        assert(m_attributes.size() == m_positions.size());
        std::memcpy(&pointer[offset], m_attributes.data(), m_attributes.size() * VertexStruct::VERTEX_ATTRIBUTE_SIZE);
        offset += m_attributes.size() * VertexStruct::VERTEX_ATTRIBUTE_SIZE;
        // Bones
        assert(m_bones.size() == m_positions.size());
        std::memcpy(&pointer[offset], m_bones.data(), m_bones.size() * VertexStruct::SKINNED_VERTEX_ATTRIBUTE_SIZE);
        offset += m_bones.size() * VertexStruct::SKINNED_VERTEX_ATTRIBUTE_SIZE;
        // Index
        std::memcpy(&pointer[offset], m_indices.data(), m_indices.size() * sizeof(uint32_t));
        offset += m_indices.size() * sizeof(uint32_t);
        assert(offset == GetExpectedBufferSize());
    }
}
