#include "Material.h"
#include "RenderSystem.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine {
    Material::Material (std::shared_ptr<RenderSystem> system) {
        m_renderSystem = system;
    }
};
