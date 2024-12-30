#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED

#include <Asset/Mesh/MeshAsset.h>
#include <Framework/component/RenderComponent/RendererComponent.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class MeshAsset;
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

        /// @brief Materialize a runtime mesh component from a mesh asset
        virtual void Init() override;
        virtual void Tick(float dt) override;

        REFL_SER_ENABLE std::shared_ptr<MeshAsset> m_mesh_asset{};
    };
}

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
