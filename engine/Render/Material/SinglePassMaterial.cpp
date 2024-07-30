#include "SinglePassMaterial.h"
#include <stdexcept>
#include "Render/RenderSystem.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine {
    SinglePassMaterial::SinglePassMaterial (
        std::shared_ptr <RenderSystem> system, 
        std::shared_ptr <ShaderPass> pass
    ) : Material(system) {
        m_pass = pass;
    }

    SinglePassMaterial::~SinglePassMaterial () {
    }
    
    void SinglePassMaterial::PrepareDraw(const MaterialDrawContext* context)
    {
        m_pass->Use();
    }
};
