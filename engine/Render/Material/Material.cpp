#include "Material.h"
#include "Render/RenderSystem.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine {
    Material::Material (std::weak_ptr <AssetManager> manager, std::shared_ptr<RenderSystem> system) 
    : Asset(manager), m_renderSystem(system) {
    }
};
