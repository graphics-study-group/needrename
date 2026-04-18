#ifndef RENDER_RENDERER_ASSETSUBMESHRENDERER_INCLUDED
#define RENDER_RENDERER_ASSETSUBMESHRENDERER_INCLUDED

#include "RuntimeRenderer.h"

#include <memory>

namespace Engine {
    class AssetRef;
    class RenderSystem;
    class StaticMeshResource;

    /**
     * @brief Runtime renderer backed by one submesh of StaticMeshResource.
     */
    class AssetSubmeshRenderer final : public RuntimeRenderer {
    private:
        uint32_t m_submesh_index{0};
        StaticMeshResource *m_mesh{};
        RenderSystemState::RenderResourceHandle m_mesh_resource{};

        AssetSubmeshRenderer(
            uint32_t layer,
            bool cast_shadow,
            bool eagerly_loaded,
            RenderSystemState::RenderResourceHandle mesh_resource,
            RenderSystemState::RenderResourceHandle material_resource,
            uint32_t submesh_index,
            StaticMeshResource *mesh
        );

    public:
        static std::unique_ptr<AssetSubmeshRenderer> Create(
            RenderSystem &system,
            const AssetRef &mesh_asset_ref,
            const AssetRef &material_asset_ref,
            uint32_t submesh_index,
            uint32_t layer,
            bool cast_shadow,
            bool eagerly_loaded
        );

        AssetSubmeshRenderer(
            uint32_t submesh_index, StaticMeshResource *mesh, uint32_t layer = 0, bool cast_shadow = false
        );

        ~AssetSubmeshRenderer() noexcept override;

        bool IsReady() const noexcept override;
        uint32_t GetIndexCount() const noexcept override;
        uint32_t GetVertexAttributeCount() const noexcept override;
        VertexAttribute GetVertexAttributeFormat() const noexcept override;
        void FillVertexAttributeBufferBindings(std::vector<BufferBindingInfo> &bindings) const noexcept override;
        BufferBindingInfo GetIndexBufferBinding() const noexcept override;

        bool IsResourcesReady(RenderSystemState::RenderResourceManager &resource_manager) const noexcept override;
    };
} // namespace Engine

#endif // RENDER_RENDERER_ASSETSUBMESHRENDERER_INCLUDED
