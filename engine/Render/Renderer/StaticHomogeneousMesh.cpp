#include "StaticHomogeneousMesh.h"

#include "Asset/Mesh/MeshAsset.h"

namespace Engine {
    void StaticHomogeneousMesh::Submit(
        const RenderSystemState::AllocatorState & allocator,
        RenderSystemState::SubmissionHelper & helper
    ) {
        // XXX: be careful about multithread synch here.
        auto & submesh_ref = data_block_ref.submeshes[submesh_index];
        if (submesh_ref.vi_buffer) {
            return;
        }

        // First load submesh info from asset...
        const auto & smi = mesh_asset.m_submeshes[submesh_index];

        submesh_ref.attributes = smi.ToVertexAttributeFormat();
        submesh_ref.index_count = smi.m_indices.size();
        submesh_ref.vertex_attribute_count = smi.vertex_count;
        auto vec = submesh_ref.attributes.EnumerateOffsetFactor();
        for (auto f : vec) {
            submesh_ref.attribute_offsets.push_back(
                f * submesh_ref.vertex_attribute_count
            );
        }
        submesh_ref.attribute_offsets.push_back(
            submesh_ref.vertex_attribute_count *
            submesh_ref.attributes.GetTotalPerVertexSize()
        );

        auto buffer_size = 
            submesh_ref.vertex_attribute_count 
            * submesh_ref.attributes.GetTotalPerVertexSize()
            + submesh_ref.index_count * sizeof(uint32_t);

        submesh_ref.vi_buffer = DeviceBuffer::CreateUnique(
            allocator,
            {BufferTypeBits::Vertex, BufferTypeBits::Index, BufferTypeBits::CopyTo},
            buffer_size
        );

        std::vector <std::byte> buf;
        buf.resize(buffer_size);
        mesh_asset.m_submeshes[submesh_index].WriteVertexAttributeBuffer(buf.data());
        mesh_asset.m_submeshes[submesh_index].WriteIndexBuffer(
            buf.data() +
            submesh_ref.vertex_attribute_count * submesh_ref.attributes.GetTotalPerVertexSize()
        );

        helper.EnqueueBufferSubmissionVertex(*submesh_ref.vi_buffer, buf);
    }

} // namespace Engine
