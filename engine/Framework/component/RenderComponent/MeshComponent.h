#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED

#include <Framework/component/RenderComponent/RendererComponent.h>
#include <Reflection/macros.h>

namespace Engine
{
    class AssetRef;
    class HomogeneousMesh;

    class REFL_SER_CLASS(REFL_WHITELIST) MeshComponent : public RendererComponent
    {
        REFL_SER_BODY(MeshComponent)
    protected:
        std::vector<std::shared_ptr<HomogeneousMesh>> m_submeshes{};

    public:
        REFL_ENABLE MeshComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~MeshComponent() = default;

        std::shared_ptr<HomogeneousMesh> GetSubmesh(uint32_t slot) const;
        auto GetSubmeshes() -> decltype(m_submeshes) &;
        auto GetSubmeshes() const -> const decltype(m_submeshes) &;

        /// @brief Materialize a runtime mesh component from a mesh asset
        virtual void RenderInit() override;
        virtual void Tick() override;

        REFL_SER_ENABLE std::shared_ptr<AssetRef> m_mesh_asset{};
    };
}

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
