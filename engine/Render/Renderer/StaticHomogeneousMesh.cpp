#include "StaticHomogeneousMesh.h"

namespace Engine {

    StaticHomogeneousMesh::StaticHomogeneousMesh(uint32_t index, StaticMeshResource *resource) :
        m_submesh_index(index), m_resource(resource) {
    }

    StaticHomogeneousMesh::~StaticHomogeneousMesh() noexcept = default;

    uint32_t StaticHomogeneousMesh::GetIndexCount() const noexcept {
        const auto &submesh = m_resource->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return submesh.index_count;
    }

    uint32_t StaticHomogeneousMesh::GetVertexAttributeCount() const noexcept {
        const auto &submesh = m_resource->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return submesh.vertex_attribute_count;
    }

    VertexAttribute StaticHomogeneousMesh::GetVertexAttributeFormat() const noexcept {
        const auto &submesh = m_resource->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return submesh.attributes;
    }

    void StaticHomogeneousMesh::FillVertexAttributeBufferBindings(
        std::vector<BufferBindingInfo> &bindings
    ) const noexcept {
        const auto &submesh_ref = m_resource->GetSubmeshData(m_submesh_index);
        assert(submesh_ref.vi_buffer);
        bindings.reserve(submesh_ref.attribute_offsets.size());
        for (auto offset : submesh_ref.attribute_offsets) {
            bindings.push_back({submesh_ref.vi_buffer.get(), offset, 0});
        }
        // Last offset is for indices.
        bindings.pop_back();
    }

    IVertexBasedRenderer::BufferBindingInfo StaticHomogeneousMesh::GetIndexBufferBinding() const noexcept {
        const auto &submesh = m_resource->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return {submesh.vi_buffer.get(), submesh.attribute_offsets.back(), 0};
    }

    bool StaticHomogeneousMesh::IsReady() const noexcept {
        return m_resource && m_resource->IsReady();
    }

} // namespace Engine
