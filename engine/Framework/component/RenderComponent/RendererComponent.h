#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED

#include <vector>
#include <Core/Math/Transform.h>
#include <Framework/component/Component.h>
#include <Asset/AssetRef.h>
#include <Reflection/macros.h>

namespace Engine
{
    class MaterialInstance;
    class RenderSystem;

    class REFL_SER_CLASS(REFL_WHITELIST) RendererComponent : public Component
    {
        REFL_SER_BODY(RendererComponent)
    protected:
        std::vector<std::shared_ptr<MaterialInstance>> m_materials{};
        std::weak_ptr<RenderSystem> m_system{};

    public:
        REFL_ENABLE RendererComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~RendererComponent() = default;

        /// @brief Get the transform which transforms local coordinate
        /// to world coordinate (i.e. the model matrix)
        /// @return Transform
        virtual Transform GetWorldTransform() const;

        virtual void Init() override;
        virtual void Tick(float dt) override;

        std::shared_ptr<MaterialInstance> GetMaterial(uint32_t slot) const;
        auto GetMaterials() -> decltype(m_materials) &;

        REFL_SER_ENABLE std::vector<std::shared_ptr<AssetRef>> m_material_assets{};
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_RENDERERCOMPONENT_INCLUDED
