#ifndef RENDER_RENDERER_HOMOGENEOUSMESHRENDERER_INCLUDED
#define RENDER_RENDERER_HOMOGENEOUSMESHRENDERER_INCLUDED

#include "RuntimeRenderer.h"

#include <vector>

namespace Engine {
    /**
     * @brief Runtime renderer for manually provided homogeneous mesh geometry.
     *
     * This renderer does not depend on mesh asset resources.
     */
    class HomogeneousMeshRenderer final : public RuntimeRenderer {
    private:
        uint32_t m_index_count{0};
        uint32_t m_vertex_attribute_count{0};
        VertexAttribute m_vertex_attribute_format{};
        std::vector<BufferBindingInfo> m_vertex_bindings{};
        BufferBindingInfo m_index_binding{};
        bool m_is_ready{false};

    public:
        HomogeneousMeshRenderer(
            uint32_t index_count,
            uint32_t vertex_attribute_count,
            VertexAttribute vertex_attribute_format,
            std::vector<BufferBindingInfo> vertex_bindings,
            BufferBindingInfo index_binding,
            RenderSystemState::RenderResourceHandle material_resource,
            uint32_t layer,
            bool cast_shadow,
            bool eagerly_loaded,
            bool is_ready = true
        );

        ~HomogeneousMeshRenderer() noexcept override;

        bool IsReady() const noexcept override;
        uint32_t GetIndexCount() const noexcept override;
        uint32_t GetVertexAttributeCount() const noexcept override;
        VertexAttribute GetVertexAttributeFormat() const noexcept override;
        void FillVertexAttributeBufferBindings(std::vector<BufferBindingInfo> &bindings) const noexcept override;
        BufferBindingInfo GetIndexBufferBinding() const noexcept override;

        bool IsResourcesReady(RenderSystemState::RenderResourceManager &resource_manager) const noexcept override;
    };
} // namespace Engine

#endif // RENDER_RENDERER_HOMOGENEOUSMESHRENDERER_INCLUDED
