#include "SinglePassMaterial.h"
#include <stdexcept>
#include "Render/RenderSystem.h"
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine {
    SinglePassMaterial::SinglePassMaterial (
        std::shared_ptr <ShaderPass> pass
    ) {
        m_pass = pass;
    }

    SinglePassMaterial::~SinglePassMaterial () {
    }

    void SinglePassMaterial::Load() {
        throw std::runtime_error("Not implemented");
    }

    void SinglePassMaterial::Unload() {
        throw std::runtime_error("Not implemented");
    }
    
    void SinglePassMaterial::PrepareDraw(const CameraContext & CameraContext, const RendererContext & RendererContext)
    {
        m_pass->Use();
    }
};
