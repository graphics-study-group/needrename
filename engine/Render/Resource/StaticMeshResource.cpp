#include "StaticMeshResource.h"

#include "Asset/Mesh/MeshAsset.h"

#include <cassert>

namespace Engine {
    StaticMeshResource::StaticMeshResource(
        GUID mesh_asset_guid, std::unique_ptr<StaticHMeshSharedDataBlock> data_block
    ) : m_mesh_asset_ref(mesh_asset_guid), m_data_block(std::move(data_block)) {
        if (!m_data_block) {
            m_data_block = std::make_unique<StaticHMeshSharedDataBlock>();
        }
    }

    bool StaticMeshResource::IsReady() const noexcept {
        if (m_data_block->submeshes.empty()) return false;
        for (uint32_t i = 0; i < m_data_block->submeshes.size(); ++i) {
            if (!static_cast<bool>(m_data_block->submeshes[i].vi_buffer)) return false;
        }
        return true;
    }

    const StaticMeshResource::StaticHMeshSharedDataBlock::PerSubmeshData &StaticMeshResource::GetSubmeshData(
        uint32_t submesh_index
    ) const noexcept {
        assert(submesh_index < m_data_block->submeshes.size());
        return m_data_block->submeshes[submesh_index];
    }

    void StaticMeshResource::Remove() noexcept {
        m_mesh_asset_ref.Release();
        m_data_block->submeshes.clear();
    }

    void StaticMeshResource::Submit(
        const RenderSystemState::AllocatorState &allocator, RenderSystemState::SubmissionHelper &helper
    ) {
        // Eagerly load the mesh asset.
        // TODO: Use memory mapping after implementing it in AssetManager, and avoid loading the whole asset into memory at once.
        m_mesh_asset_ref.Acquire();
        auto *mesh_asset = m_mesh_asset_ref.as<MeshAsset>();
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

            helper.EnqueueBufferSubmission(*submesh_ref.vi_buffer, buf);
        }

        m_mesh_asset_ref.Release();
    }
} // namespace Engine
