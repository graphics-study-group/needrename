#include "StaticMeshResource.h"

#include "Asset/Mesh/MeshAsset.h"

#include <cassert>

namespace Engine {
    StaticMeshResource::StaticMeshResource(
        const MeshAsset &asset, std::unique_ptr<StaticHMeshSharedDataBlock> data_block
    ) : m_mesh_asset(asset), m_data_block(std::move(data_block)) {
        if (!m_data_block) {
            m_data_block = std::make_unique<StaticHMeshSharedDataBlock>();
        }
        m_data_block->submeshes.resize(m_mesh_asset.GetSubmeshCount());
    }

    size_t StaticMeshResource::GetSubmeshCount() const noexcept {
        return m_mesh_asset.GetSubmeshCount();
    }

    bool StaticMeshResource::IsReady() const noexcept {
        for (uint32_t i = 0; i < m_data_block->submeshes.size(); ++i) {
            if (!IsSubmeshReady(i)) return false;
        }
        return true;
    }

    bool StaticMeshResource::IsSubmeshReady(uint32_t submesh_index) const noexcept {
        assert(submesh_index < m_data_block->submeshes.size());
        return static_cast<bool>(m_data_block->submeshes[submesh_index].vi_buffer);
    }

    const StaticMeshResource::StaticHMeshSharedDataBlock::PerSubmeshData &StaticMeshResource::GetSubmeshData(
        uint32_t submesh_index
    ) const noexcept {
        assert(submesh_index < m_data_block->submeshes.size());
        return m_data_block->submeshes[submesh_index];
    }

    void StaticMeshResource::EnsurePrepared(
        const RenderSystemState::AllocatorState &allocator, RenderSystemState::SubmissionHelper &helper
    ) {
        for (uint32_t submesh_index = 0; submesh_index < m_mesh_asset.GetSubmeshCount(); ++submesh_index) {
            auto &submesh_ref = m_data_block->submeshes[submesh_index];
            if (submesh_ref.vi_buffer) {
                continue;
            }

            const auto &smi = m_mesh_asset.m_submeshes[submesh_index];
            submesh_ref.attributes = smi.ToVertexAttributeFormat();
            submesh_ref.index_count = static_cast<uint32_t>(smi.m_indices.size());
            submesh_ref.vertex_attribute_count = smi.vertex_count;
            submesh_ref.attribute_offsets.clear();

            auto vec = submesh_ref.attributes.EnumerateOffsetFactor();
            for (auto factor : vec) {
                submesh_ref.attribute_offsets.push_back(factor * submesh_ref.vertex_attribute_count);
            }
            submesh_ref.attribute_offsets.push_back(
                submesh_ref.vertex_attribute_count * submesh_ref.attributes.GetTotalPerVertexSize()
            );

            auto buffer_size = submesh_ref.vertex_attribute_count * submesh_ref.attributes.GetTotalPerVertexSize()
                               + submesh_ref.index_count * sizeof(uint32_t);

            submesh_ref.vi_buffer = DeviceBuffer::CreateUnique(
                allocator, {BufferTypeBits::Vertex, BufferTypeBits::Index, BufferTypeBits::CopyTo}, buffer_size
            );

            std::vector<std::byte> buf;
            buf.resize(buffer_size);
            smi.WriteVertexAttributeBuffer(buf.data());
            smi.WriteIndexBuffer(
                buf.data() + submesh_ref.vertex_attribute_count * submesh_ref.attributes.GetTotalPerVertexSize()
            );

            helper.EnqueueBufferSubmissionVertex(*submesh_ref.vi_buffer, buf);
        }
    }
} // namespace Engine
