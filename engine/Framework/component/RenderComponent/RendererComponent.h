#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED

#include "Core/Math/Transform.h"
#include "Framework/component/Component.h"
#include <vector>

namespace Engine
{
    class Material;
    class MaterialAsset;
    class RenderSystem;

    class RendererComponent : public Component
    {
    protected:
        std::vector<std::shared_ptr<Material>> m_materials{};
        std::weak_ptr <RenderSystem> m_system;
    public:
        RendererComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~RendererComponent() = 0;

        /// @brief Get the transform which transforms local coordinate 
        /// to world coordinate (i.e. the model matrix)
        /// @return Transform
        virtual Transform GetWorldTransform() const;

        virtual void Init() override;
        virtual void Tick(float dt) override;

        std::shared_ptr <Material> GetMaterial (uint32_t slot) const;
        auto GetMaterials () -> decltype(m_materials) &;

        std::vector<std::shared_ptr<MaterialAsset>> m_material_assets;
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
