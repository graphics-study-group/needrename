#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED

#include <vector>
#include <Core/Math/Transform.h>
#include <Framework/component/Component.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class Material;
    class MaterialAsset;
    class RenderSystem;

    class REFL_SER_CLASS(REFL_WHITELIST) RendererComponent : public Component
    {
        REFL_SER_BODY()
    protected:
        std::vector<std::shared_ptr<Material>> m_materials{};
        std::weak_ptr<RenderSystem> m_system{};

    public:
        REFL_ENABLE RendererComponent() = default;
        REFL_ENABLE RendererComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~RendererComponent() = default;

        /// @brief Get the transform which transforms local coordinate
        /// to world coordinate (i.e. the model matrix)
        /// @return Transform
        virtual Transform GetWorldTransform() const;

        virtual void Init() override;
        virtual void Tick(float dt) override;

        std::shared_ptr<Material> GetMaterial(uint32_t slot) const;
        auto GetMaterials() -> decltype(m_materials) &;

        REFL_SER_ENABLE std::vector<std::shared_ptr<MaterialAsset>> m_material_assets{};
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
