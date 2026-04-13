#include "Render/Resource/StaticMeshResource.h"

#include "Asset/Mesh/MeshAsset.h"
#include "Render/Memory/DeviceBuffer.h"

namespace Engine::RenderSystemState {
    StaticMeshResource::StaticMeshResource(const GUID &mesh_guid) : m_mesh_asset_ref(mesh_guid), m_submeshes() {
        auto *mesh_asset = m_mesh_asset_ref.as<MeshAsset>();
        assert(mesh_asset);
        m_submeshes.resize(mesh_asset->GetSubmeshCount());
    }

    StaticMeshResource::~StaticMeshResource() = default;

    size_t StaticMeshResource::GetSubmeshCount() const noexcept {
        return m_submeshes.size();
    }

    bool StaticMeshResource::IsReady(uint32_t submesh_index) const noexcept {
        assert(submesh_index < m_submeshes.size());
        return static_cast<bool>(m_submeshes[submesh_index].vi_buffer);
    }

    uint32_t StaticMeshResource::GetIndexCount(uint32_t submesh_index) const noexcept {
        assert(submesh_index < m_submeshes.size());
        assert(m_submeshes[submesh_index].vi_buffer);
        return m_submeshes[submesh_index].index_count;
    }

    uint32_t StaticMeshResource::GetVertexAttributeCount(uint32_t submesh_index) const noexcept {
        assert(submesh_index < m_submeshes.size());
        assert(m_submeshes[submesh_index].vi_buffer);
        return m_submeshes[submesh_index].vertex_attribute_count;
    }

    VertexAttribute StaticMeshResource::GetVertexAttributeFormat(uint32_t submesh_index) const noexcept {
        assert(submesh_index < m_submeshes.size());
        assert(m_submeshes[submesh_index].vi_buffer);
        return m_submeshes[submesh_index].attributes;
    }

    void StaticMeshResource::FillVertexAttributeBufferBindings(
        uint32_t submesh_index, std::vector<IVertexBasedRenderer::BufferBindingInfo> &bindings
    ) const noexcept {
        assert(submesh_index < m_submeshes.size());

        const auto &submesh = m_submeshes[submesh_index];
        assert(submesh.vi_buffer);
        bindings.reserve(submesh.attribute_offsets.size());
        for (auto offset : submesh.attribute_offsets) {
            bindings.push_back({submesh.vi_buffer.get(), offset, 0});
        }
        bindings.pop_back();
    }

    IVertexBasedRenderer::BufferBindingInfo StaticMeshResource::GetIndexBufferBinding(
        uint32_t submesh_index
    ) const noexcept {
        assert(submesh_index < m_submeshes.size());

        const auto &submesh = m_submeshes[submesh_index];
        assert(submesh.vi_buffer);
        return {submesh.vi_buffer.get(), submesh.attribute_offsets.back(), 0};
    }

    void StaticMeshResource::EnsureUploaded(
        uint32_t submesh_index, const AllocatorState &allocator, SubmissionHelper &helper
    ) {
        assert(submesh_index < m_submeshes.size());

        auto &submesh = m_submeshes[submesh_index];
        if (submesh.vi_buffer) {
            return;
        }

        const auto &mesh_asset = GetMeshAsset();
        const auto &asset_submesh = mesh_asset.m_submeshes[submesh_index];

        submesh.attributes = asset_submesh.ToVertexAttributeFormat();
        submesh.index_count = asset_submesh.m_indices.size();
        submesh.vertex_attribute_count = asset_submesh.vertex_count;

        auto offsets = submesh.attributes.EnumerateOffsetFactor();
        submesh.attribute_offsets.reserve(offsets.size() + 1);
        for (auto factor : offsets) {
            submesh.attribute_offsets.push_back(factor * submesh.vertex_attribute_count);
        }
        submesh.attribute_offsets.push_back(
            submesh.vertex_attribute_count * submesh.attributes.GetTotalPerVertexSize()
        );

        const auto buffer_size = submesh.vertex_attribute_count * submesh.attributes.GetTotalPerVertexSize()
                                 + submesh.index_count * sizeof(uint32_t);
        submesh.vi_buffer = DeviceBuffer::CreateUnique(
            allocator, {BufferTypeBits::Vertex, BufferTypeBits::Index, BufferTypeBits::CopyTo}, buffer_size
        );

        std::vector<std::byte> host_buffer(buffer_size);
        asset_submesh.WriteVertexAttributeBuffer(host_buffer.data());
        asset_submesh.WriteIndexBuffer(
            host_buffer.data() + submesh.vertex_attribute_count * submesh.attributes.GetTotalPerVertexSize()
        );
        helper.EnqueueBufferSubmissionVertex(*submesh.vi_buffer, host_buffer);
    }

    MeshAsset &StaticMeshResource::GetMeshAsset() {
        auto *mesh_asset = m_mesh_asset_ref.as<MeshAsset>();
        assert(mesh_asset);
        return *mesh_asset;
    }

    const MeshAsset &StaticMeshResource::GetMeshAsset() const {
        auto *mesh_asset = m_mesh_asset_ref.cas<MeshAsset>();
        assert(mesh_asset);
        return *mesh_asset;
    }
} // namespace Engine::RenderSystemState
