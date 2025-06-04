#include "HomogeneousMesh.h"
#include <vulkan/vulkan.hpp>
#include <Asset/AssetRef.h>
#include <Asset/Mesh/MeshAsset.h>
#include <SDL3/SDL.h>

namespace Engine {
    HomogeneousMesh::HomogeneousMesh(
        std::weak_ptr<RenderSystem> system,
        std::shared_ptr<AssetRef> mesh_asset,
        size_t submesh_idx
    ) : m_system(system), 
    m_buffer(system.lock()), 
    m_mesh_asset(mesh_asset), 
    m_submesh_idx(submesh_idx) {
    }

    HomogeneousMesh::~HomogeneousMesh() {
    }

    void HomogeneousMesh::Prepare() {
        const uint32_t new_vertex_count = GetVertexCount();
        const uint64_t buffer_size = GetExpectedBufferSize();

        if (m_allocated_buffer_size != buffer_size) {
            m_updated = true;
            SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, 
                "(Re-)Allocating buffer and memory for %u vertices (%llu bytes).", 
                new_vertex_count, buffer_size
            );
            m_buffer.Create(Buffer::BufferType::Vertex, buffer_size, "Buffer - mesh vertices");
            m_allocated_buffer_size = buffer_size;
        }
    }

    bool HomogeneousMesh::NeedCommitment() {
        bool need = m_updated;
        m_updated = false;
        return need;
    }

    Buffer HomogeneousMesh::CreateStagingBuffer() const {
        const uint64_t buffer_size = GetExpectedBufferSize();

        Buffer buffer{m_system.lock()};
        buffer.Create(Buffer::BufferType::Staging, buffer_size, "Buffer - mesh staging");

        std::byte * data = buffer.Map();
        WriteToMemory(data);
        buffer.Flush();
        buffer.Unmap();

        return buffer;
    }

    void HomogeneousMesh::WriteToMemory(std::byte* pointer) const {
        uint64_t offset = 0;
        auto &mesh_asset = *m_mesh_asset->as<MeshAsset>();
        const auto &positions = mesh_asset.m_submeshes[m_submesh_idx].m_positions;
        const auto &attributes = mesh_asset.m_submeshes[m_submesh_idx].m_attributes;
        const auto &indices = mesh_asset.m_submeshes[m_submesh_idx].m_indices;
        // Position
        std::memcpy(&pointer[offset], positions.data(), positions.size() * VertexStruct::VERTEX_POSITION_SIZE);
        offset += positions.size() * VertexStruct::VERTEX_POSITION_SIZE;
        // Attributes
        std::memcpy(&pointer[offset], attributes.data(), attributes.size() * VertexStruct::VERTEX_ATTRIBUTE_SIZE);
        offset += attributes.size() * VertexStruct::VERTEX_ATTRIBUTE_SIZE;
        // Index
        std::memcpy(&pointer[offset], indices.data(), indices.size() * sizeof(uint32_t));
        offset += indices.size() * sizeof(uint32_t);
        assert(offset == GetExpectedBufferSize());
    }

    vk::PipelineVertexInputStateCreateInfo HomogeneousMesh::GetVertexInputState() {
        return vk::PipelineVertexInputStateCreateInfo{
            vk::PipelineVertexInputStateCreateFlags{0}, bindings, attributes
        };
    }

    std::pair<vk::Buffer, vk::DeviceSize> HomogeneousMesh::GetIndexInfo() const {
        assert(this->m_buffer.GetBuffer());
        uint64_t total_size = GetVertexCount() * VertexStruct::VERTEX_TOTAL_SIZE;
        return std::make_pair(m_buffer.GetBuffer(), total_size);
    }

    uint32_t HomogeneousMesh::GetVertexIndexCount() const {
        return m_mesh_asset->as<MeshAsset>()->GetSubmeshVertexIndexCount(m_submesh_idx);
    }

    uint32_t HomogeneousMesh::GetVertexCount() const {
        return m_mesh_asset->as<MeshAsset>()->GetSubmeshVertexCount(m_submesh_idx);
    }

    uint64_t HomogeneousMesh::GetExpectedBufferSize() const {
        return m_mesh_asset->as<MeshAsset>()->GetSubmeshExpectedBufferSize(m_submesh_idx);
    }

    const Buffer & HomogeneousMesh::GetBuffer() const {
        return m_buffer;
    }

    std::pair <std::array<vk::Buffer, HomogeneousMesh::BINDING_COUNT>, std::array<vk::DeviceSize, HomogeneousMesh::BINDING_COUNT>> 
    HomogeneousMesh::GetBindingInfo() const {
        assert(this->m_buffer.GetBuffer());
        std::array<vk::Buffer, BINDING_COUNT> buffer {this->m_buffer.GetBuffer(), this->m_buffer.GetBuffer()};
        std::array<vk::DeviceSize, BINDING_COUNT> binding_offset {0, GetVertexCount() * VertexStruct::VERTEX_POSITION_SIZE};
        return std::make_pair(buffer, binding_offset);
    }
}
