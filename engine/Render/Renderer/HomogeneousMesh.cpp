#include "HomogeneousMesh.h"
#include <Asset/AssetRef.h>
#include <Asset/Mesh/MeshAsset.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

namespace Engine {

    struct HomogeneousMesh::impl {
        std::unique_ptr <Buffer> m_buffer {};
        std::vector <vk::DeviceSize> m_buffer_offsets {};

        bool m_updated {false};

        uint64_t m_total_allocated_buffer_size {0};

        std::shared_ptr<AssetRef> m_mesh_asset {};
        size_t m_submesh_idx {};
        MeshVertexType m_type {};

        void WriteToMemory(std::byte * pointer) const;
        /**
         * @brief Allocate buffer and update pre-calculated offsets.
         * Called before `CreateStagingBuffer()`.
         */
        void FetchFromAsset();
        uint32_t GetVertexIndexCount() const;
        uint32_t GetVertexCount() const;
        uint64_t GetExpectedBufferSize() const;
    };

    HomogeneousMesh::HomogeneousMesh(
        std::weak_ptr<RenderSystem> system,
        std::shared_ptr<AssetRef> mesh_asset,
        size_t submesh_idx,
        MeshVertexType type
    ) : m_system(system), pimpl(std::make_unique<impl>()) {
        pimpl->m_mesh_asset = mesh_asset;
        pimpl->m_submesh_idx = submesh_idx;

        assert(type == MeshVertexType::Basic && "Unimplemented");
        pimpl->m_type = type;
        pimpl->m_buffer = std::make_unique<Buffer>(system.lock());

        pimpl->FetchFromAsset();
    }

    HomogeneousMesh::~HomogeneousMesh() {
    }

    void HomogeneousMesh::impl::FetchFromAsset() {
        const uint64_t buffer_size = GetExpectedBufferSize();

        if (m_total_allocated_buffer_size != buffer_size) {
            m_total_allocated_buffer_size = 0;
            m_updated = true;

            const uint32_t new_vertex_count = GetVertexCount();
            const uint32_t new_vertex_index_count = GetVertexIndexCount();
            SDL_LogVerbose(
                SDL_LOG_CATEGORY_RENDER,
                "(Re-)Allocating buffer and memory for %u vertices and %u indices (%llu bytes).",
                new_vertex_count,
                new_vertex_index_count,
                buffer_size
            );
            m_buffer->Create(Buffer::BufferType::Vertex, buffer_size, "Buffer - mesh vertices");
            m_total_allocated_buffer_size = buffer_size;

            // Generate buffer offsets
            m_buffer_offsets.clear();
            m_buffer_offsets.push_back(0);
            switch(m_type) {
            case MeshVertexType::Skinned:
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexPosition));
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexAttributeBasic) + *m_buffer_offsets.rbegin());
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexAttributeExtended) + *m_buffer_offsets.rbegin());
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexAttributeSkinned) + *m_buffer_offsets.rbegin());
                break;
            case MeshVertexType::Extended:
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexPosition));
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexAttributeBasic) + *m_buffer_offsets.rbegin());
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexAttributeExtended) + *m_buffer_offsets.rbegin());
                break;
            case MeshVertexType::Basic:
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexPosition));
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexAttributeBasic) + *m_buffer_offsets.rbegin());
            }

            assert(*m_buffer_offsets.rbegin() == buffer_size - new_vertex_index_count * sizeof(uint32_t));
        }
    }

    Buffer HomogeneousMesh::CreateStagingBuffer() const {
        pimpl->FetchFromAsset();

        const uint64_t buffer_size = GetExpectedBufferSize();

        Buffer buffer{m_system.lock()};
        buffer.Create(Buffer::BufferType::Staging, buffer_size, "Buffer - mesh staging");

        std::byte *data = buffer.Map();
        pimpl->WriteToMemory(data);
        buffer.Flush();
        buffer.Unmap();

        return buffer;
    }

    void HomogeneousMesh::impl::WriteToMemory(std::byte *pointer) const {
        uint64_t offset = 0;
        auto &mesh_asset = *m_mesh_asset->as<MeshAsset>();
        const auto &positions = mesh_asset.m_submeshes[m_submesh_idx].m_positions;
        const auto &attributes = mesh_asset.m_submeshes[m_submesh_idx].m_attributes_basic;
        const auto &indices = mesh_asset.m_submeshes[m_submesh_idx].m_indices;
        // Position
        std::memcpy(&pointer[offset], positions.data(), positions.size() * sizeof(VertexStruct::VertexPosition));
        offset += positions.size() * sizeof(VertexStruct::VertexPosition);
        // Attributes
        std::memcpy(&pointer[offset], attributes.data(), attributes.size() * sizeof(VertexStruct::VertexAttributeBasic));
        offset += attributes.size() * sizeof(VertexStruct::VertexAttributeBasic);
        // Index
        std::memcpy(&pointer[offset], indices.data(), indices.size() * sizeof(uint32_t));
        offset += indices.size() * sizeof(uint32_t);

    }

    vk::PipelineVertexInputStateCreateInfo HomogeneousMesh::GetVertexInputState(MeshVertexType type) {
        assert(type == MeshVertexType::Basic && "Unimplemented");
        return vk::PipelineVertexInputStateCreateInfo{
            vk::PipelineVertexInputStateCreateFlags{0}, 
            VertexStruct::BINDINGS_BASIC,
            VertexStruct::ATTRIBUTES_BASIC
        };
    }

    std::pair<vk::Buffer, vk::DeviceSize> HomogeneousMesh::GetIndexBufferInfo() const {
        assert(pimpl->m_buffer->GetBuffer());
        // Last offset is the offset of index buffer.
        return std::make_pair(pimpl->m_buffer->GetBuffer(), *(pimpl->m_buffer_offsets.rbegin()));
    }

    uint32_t HomogeneousMesh::impl::GetVertexIndexCount() const {
        return m_mesh_asset->as<MeshAsset>()->GetSubmeshVertexIndexCount(m_submesh_idx);
    }

    uint32_t HomogeneousMesh::GetVertexIndexCount() const {
        return pimpl->GetVertexIndexCount();
    }

    uint32_t HomogeneousMesh::impl::GetVertexCount() const {
        return m_mesh_asset->as<MeshAsset>()->GetSubmeshVertexCount(m_submesh_idx);
    }
    uint32_t HomogeneousMesh::GetVertexCount() const {
        return pimpl->GetVertexCount();
    }

    uint64_t HomogeneousMesh::impl::GetExpectedBufferSize() const {
        auto vertex_cnt = GetVertexCount();
        uint64_t size = GetVertexIndexCount() * sizeof(uint32_t);
        switch(m_type) {
            case MeshVertexType::Skinned:
                size += vertex_cnt * sizeof(VertexStruct::VertexAttributeSkinned);
                [[fallthrough]];
            case MeshVertexType::Extended:
                size += vertex_cnt * sizeof(VertexStruct::VertexAttributeExtended);
                [[fallthrough]];
            case MeshVertexType::Basic:
                size += vertex_cnt * sizeof(VertexStruct::VertexAttributeBasic);
                [[fallthrough]];
            case MeshVertexType::Position:
                size += vertex_cnt * sizeof(VertexStruct::VertexPosition);
        }
        return size;
    }
    uint64_t HomogeneousMesh::GetExpectedBufferSize() const {
        return pimpl->GetExpectedBufferSize();
    }

    const Buffer &HomogeneousMesh::GetBuffer() const {
        return *pimpl->m_buffer;
    }

    std::pair<vk::Buffer, std::vector<vk::DeviceSize>>
    HomogeneousMesh::GetVertexBufferInfo() const {
        assert(pimpl->m_buffer->GetBuffer());
        return std::make_pair(pimpl->m_buffer->GetBuffer(), pimpl->m_buffer_offsets);
    }
} // namespace Engine
