#include "HomogeneousMeshRenderer.h"

#include <utility>

namespace Engine {
    HomogeneousMeshRenderer::HomogeneousMeshRenderer(
        uint32_t index_count,
        uint32_t vertex_attribute_count,
        VertexAttribute vertex_attribute_format,
        std::vector<BufferBindingInfo> vertex_bindings,
        BufferBindingInfo index_binding,
        RenderSystemState::RenderResourceHandle material_resource,
        uint32_t layer,
        bool cast_shadow,
        bool eagerly_loaded,
        bool is_ready
    ) :
        RuntimeRenderer(layer, cast_shadow, eagerly_loaded), m_index_count(index_count),
        m_vertex_attribute_count(vertex_attribute_count), m_vertex_attribute_format(vertex_attribute_format),
        m_vertex_bindings(std::move(vertex_bindings)), m_index_binding(index_binding), m_is_ready(is_ready) {
        SetResourceHandle(MATERIAL_RESOURCE_SLOT, material_resource);
    }

    HomogeneousMeshRenderer::~HomogeneousMeshRenderer() noexcept = default;

    bool HomogeneousMeshRenderer::IsReady() const noexcept {
        return m_is_ready;
    }

    uint32_t HomogeneousMeshRenderer::GetIndexCount() const noexcept {
        return m_index_count;
    }

    uint32_t HomogeneousMeshRenderer::GetVertexAttributeCount() const noexcept {
        return m_vertex_attribute_count;
    }

    VertexAttribute HomogeneousMeshRenderer::GetVertexAttributeFormat() const noexcept {
        return m_vertex_attribute_format;
    }

    void HomogeneousMeshRenderer::FillVertexAttributeBufferBindings(
        std::vector<BufferBindingInfo> &bindings
    ) const noexcept {
        bindings.insert(bindings.end(), m_vertex_bindings.begin(), m_vertex_bindings.end());
    }

    RuntimeRenderer::BufferBindingInfo HomogeneousMeshRenderer::GetIndexBufferBinding() const noexcept {
        return m_index_binding;
    }

    bool HomogeneousMeshRenderer::IsResourcesReady(RenderSystemState::RenderResourceManager &) const noexcept {
        return IsReady();
    }
} // namespace Engine
