#ifndef RENDER_RESOURCE_STATICMESHRESOURCE_INCLUDED
#define RENDER_RESOURCE_STATICMESHRESOURCE_INCLUDED

#include "Asset/AssetRef.h"
#include "Render/Renderer/IVertexBasedRenderer.h"
#include "Render/Renderer/VertexAttribute.h"

#include <memory>
#include <vector>

namespace Engine {
    class MeshAsset;
    class DeviceBuffer;

    namespace RenderSystemState {
        class AllocatorState;
        class SubmissionHelper;

        class StaticMeshResource final {
        public:
            struct PerSubmeshData {
                VertexAttribute attributes;
                uint32_t vertex_attribute_count{0};
                uint32_t index_count{0};

                std::vector<uint32_t> attribute_offsets;
                std::unique_ptr<DeviceBuffer> vi_buffer;
            };

        private:
            AssetRef m_mesh_asset_ref;
            std::vector<PerSubmeshData> m_submeshes;

            MeshAsset &GetMeshAsset();
            const MeshAsset &GetMeshAsset() const;

        public:
            explicit StaticMeshResource(const GUID &mesh_guid);
            ~StaticMeshResource();

            size_t GetSubmeshCount() const noexcept;
            bool IsReady(uint32_t submesh_index) const noexcept;
            uint32_t GetIndexCount(uint32_t submesh_index) const noexcept;
            uint32_t GetVertexAttributeCount(uint32_t submesh_index) const noexcept;
            VertexAttribute GetVertexAttributeFormat(uint32_t submesh_index) const noexcept;
            void FillVertexAttributeBufferBindings(
                uint32_t submesh_index, std::vector<IVertexBasedRenderer::BufferBindingInfo> &bindings
            ) const noexcept;
            IVertexBasedRenderer::BufferBindingInfo GetIndexBufferBinding(uint32_t submesh_index) const noexcept;
            void EnsureUploaded(uint32_t submesh_index, const AllocatorState &allocator, SubmissionHelper &helper);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_STATICMESHRESOURCE_INCLUDED
