#ifndef COMPONENT_RENDERCOMPONENT_STATICMESHCOMPONENT_INCLUDED
#define COMPONENT_RENDERCOMPONENT_STATICMESHCOMPONENT_INCLUDED

#include "RendererComponent.h"

namespace Engine {
    class AssetRef;

    class REFL_SER_CLASS(REFL_WHITELIST) StaticMeshComponent : public RendererComponent {
        REFL_SER_BODY(StaticMeshComponent)
    public:

        StaticMeshComponent(std::weak_ptr <GameObject> go) : RendererComponent(go) {};
        virtual ~StaticMeshComponent() = default;

        REFL_ENABLE std::shared_ptr<AssetRef> m_mesh_asset{};
    };
}

#endif // COMPONENT_RENDERCOMPONENT_STATICMESHCOMPONENT_INCLUDED
