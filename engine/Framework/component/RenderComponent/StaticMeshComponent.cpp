#include "StaticMeshComponent.h"

#include "MainClass.h"
#include "Render/RenderSystem.h"

namespace Engine {
    void StaticMeshComponent::Awake() {
        RendererComponent::Awake();
        auto system = MainClass::GetInstance()->GetRenderSystem();
        m_renderer_handles = system->GetRendererManager().RegisterRenderer(
            m_mesh_asset, m_material_assets, m_layer, m_cast_shadow, m_is_eagerly_loaded
        );
    }
} // namespace Engine
