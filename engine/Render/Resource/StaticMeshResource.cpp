#include "StaticMeshResource.h"

#include "Asset/Mesh/MeshAsset.h"

#include <cassert>

namespace Engine {
    StaticMeshResource::StaticMeshResource(
        AssetRef mesh_asset_ref, std::unique_ptr<StaticHMeshSharedDataBlock> data_block
    ) : m_mesh_asset_ref(std::move(mesh_asset_ref)), m_data_block(std::move(data_block)) {
        if (!m_data_block) {
            m_data_block = std::make_unique<StaticHMeshSharedDataBlock>();
        }
    }

    bool StaticMeshResource::IsReady() const noexcept {
        if (m_data_block->submeshes.empty()) return false;
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

    bool StaticMeshResource::Submit(
        const RenderSystemState::AllocatorState &allocator, RenderSystemState::SubmissionHelper &helper, bool async_load
    ) {
        auto *mesh_asset = m_mesh_asset_ref.as<MeshAsset>(async_load);
        if (async_load && !mesh_asset) {
            // Asset is not ready yet. Submission will be attempted again in the next frame.
            return false;
        }
        assert(mesh_asset);
        m_data_block->submeshes.resize(mesh_asset->GetSubmeshCount());

        for (uint32_t submesh_index = 0; submesh_index < mesh_asset->GetSubmeshCount(); ++submesh_index) {
            auto &submesh_ref = m_data_block->submeshes[submesh_index];
            if (submesh_ref.vi_buffer) {
                continue;
            }

            const auto &smi = mesh_asset->m_submeshes[submesh_index];
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

        m_mesh_asset_ref.Release();
        return true;
    }
} // namespace Engine
