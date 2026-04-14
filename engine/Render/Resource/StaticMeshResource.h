#ifndef RENDER_RESOURCE_STATICMESHRESOURCE_INCLUDED
#define RENDER_RESOURCE_STATICMESHRESOURCE_INCLUDED

#include "Asset/AssetRef.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/Renderer/VertexAttribute.h"

#include <memory>
#include <vector>

namespace Engine {
    namespace RenderSystemState {
        class AllocatorState;
        class SubmissionHelper;
    } // namespace RenderSystemState

    /**
     * @brief GPU-side static mesh resource prepared from one MeshAsset.
     *
     * One StaticMeshResource owns the prepared data for all submeshes of a mesh
     * asset. Individual submesh renderers read from this resource at draw time.
     */
    class StaticMeshResource {
    public:
        struct StaticHMeshSharedDataBlock {
            struct PerSubmeshData {
                VertexAttribute attributes{};
                uint32_t vertex_attribute_count{0};
                uint32_t index_count{0};

                std::vector<uint32_t> attribute_offsets{};
                std::unique_ptr<DeviceBuffer> vi_buffer{};
            };

            std::vector<PerSubmeshData> submeshes{};
        };

    private:
        AssetRef m_mesh_asset_ref{};
        std::unique_ptr<StaticHMeshSharedDataBlock> m_data_block;

    public:
        explicit StaticMeshResource(AssetRef mesh_asset_ref, std::unique_ptr<StaticHMeshSharedDataBlock> data_block = nullptr);

        /**
         * @brief Whether all submeshes in this resource are ready for rendering.
         * A submesh is ready if its vertex/index buffer is prepared and valid.
         */
        bool IsReady() const noexcept;

        /**
         * @brief Get the prepared data of a specific submesh in this resource.
         * The submesh must be ready before calling this method.
         *
         * @param submesh_index Index of the submesh to query. Must be smaller than `GetSubmeshCount()`.
         * @return Reference to the prepared data of the submesh. The returned reference is valid as long as this resource is not destructed or re-prepared.
         */
        const StaticHMeshSharedDataBlock::PerSubmeshData &GetSubmeshData(uint32_t submesh_index) const noexcept;

        /**
         * @brief Ensure that all submeshes in this resource are prepared for rendering.
         *
         * The source MeshAsset is intentionally kept alive by AssetRef until GPU
         * upload has completed. After preparation succeeds, the asset reference
         * is released so CPU-side mesh memory may be reclaimed independently of
         * the lifetime of the GPU buffers stored by this resource.
         *
         * @param allocator_state The allocator state to use for preparing the submeshes.
         * @param submission_helper The submission helper to use for preparing the submeshes.
         */
        void EnsurePrepared(const RenderSystemState::AllocatorState &, RenderSystemState::SubmissionHelper &);
    };
} // namespace Engine

#endif // RENDER_RESOURCE_STATICMESHRESOURCE_INCLUDED
