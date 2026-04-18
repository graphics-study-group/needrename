#include "RuntimeRenderer.h"

namespace Engine {
    RuntimeRenderer::RuntimeRenderer(uint32_t layer, bool cast_shadow, bool eagerly_loaded) noexcept :
        m_layer(layer), m_cast_shadow(cast_shadow), m_is_eagerly_loaded(eagerly_loaded) {
    }

    void RuntimeRenderer::SetResourceHandle(uint32_t slot, RenderSystemState::RenderResourceHandle handle) {
        if (m_resource_handles.size() <= slot) {
            m_resource_handles.resize(slot + 1);
        }
        m_resource_handles[slot] = handle;
    }

    uint32_t RuntimeRenderer::GetLayer() const noexcept {
        return m_layer;
    }

    bool RuntimeRenderer::CastShadow() const noexcept {
        return m_cast_shadow;
    }

    bool RuntimeRenderer::IsEagerlyLoaded() const noexcept {
        return m_is_eagerly_loaded;
    }

    RenderSystemState::RenderResourceHandle RuntimeRenderer::GetResourceHandle(uint32_t slot) const noexcept {
        if (slot >= m_resource_handles.size()) return {};
        return m_resource_handles[slot];
    }

    const std::vector<RenderSystemState::RenderResourceHandle> &RuntimeRenderer::GetResourceHandles() const noexcept {
        return m_resource_handles;
    }
} // namespace Engine
