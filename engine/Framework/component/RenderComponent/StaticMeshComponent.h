#ifndef COMPONENT_RENDERCOMPONENT_STATICMESHCOMPONENT_INCLUDED
#define COMPONENT_RENDERCOMPONENT_STATICMESHCOMPONENT_INCLUDED

#include "RendererComponent.h"

namespace Engine {
    class AssetRef;
    class GameObject;

    /**
     * @brief A component for static mesh that cannot be animated.
     *
     * All static mesh components that are instantiated by the same asset will
     * share their memory for vertex and index buffers.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) StaticMeshComponent : public RendererComponent {
        REFL_SER_BODY(StaticMeshComponent)
    public:
        REFL_ENABLE StaticMeshComponent(const GameObject &parent) : RendererComponent(parent) {};
        virtual ~StaticMeshComponent() = default;

        void Awake() override;

        REFL_SER_ENABLE AssetRef m_mesh_asset{};
    };
} // namespace Engine

#endif // COMPONENT_RENDERCOMPONENT_STATICMESHCOMPONENT_INCLUDED
