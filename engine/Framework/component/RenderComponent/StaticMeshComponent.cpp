#include "StaticMeshComponent.h"

#include "MainClass.h"
#include "Render/RenderSystem.h"

namespace Engine {
    void StaticMeshComponent::Awake() {
        RendererComponent::Awake();
        auto system = MainClass::GetInstance()->GetRenderSystem();
        m_renderer_handles.clear();
        for (size_t i = 0; i < m_material_assets.size(); i++) {
            m_renderer_handles.push_back(system->GetRendererManager().RegisterRenderer(
                m_mesh_asset, m_material_assets[i], i, m_layer, m_cast_shadow, m_is_eagerly_loaded
            ));
        }
    }
} // namespace Engine
