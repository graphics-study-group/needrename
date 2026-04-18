#include "StaticMeshComponent.h"

#include "MainClass.h"
#include "Render/RenderSystem.h"
#include "Render/Renderer/AssetSubmeshRenderer.h"

#include <utility>

namespace Engine {
    void StaticMeshComponent::Awake() {
        RendererComponent::Awake();
        auto system = MainClass::GetInstance()->GetRenderSystem();
        m_renderer_handles.clear();
        for (size_t i = 0; i < m_material_assets.size(); i++) {
            auto renderer = AssetSubmeshRenderer::Create(
                *system,
                m_mesh_asset,
                m_material_assets[i],
                static_cast<uint32_t>(i),
                m_layer,
                m_cast_shadow,
                m_is_eagerly_loaded
            );
            m_renderer_handles.push_back(system->GetRendererManager().RegisterRenderer(std::move(renderer)));
        }
    }
} // namespace Engine
