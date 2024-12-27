#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED

#include <Asset/Mesh/MeshAsset.h>
#include <Framework/component/RenderComponent/RendererComponent.h>

namespace Engine
{
    class MeshAsset;
    class HomogeneousMesh;

    class MeshComponent : public RendererComponent
    {
    protected:
        std::vector<std::shared_ptr<HomogeneousMesh>> m_submeshes;

        void Materialize();

    public:
        MeshComponent(std::weak_ptr<GameObject> gameObject);

        std::shared_ptr<HomogeneousMesh> GetSubmesh(uint32_t slot) const;
        auto GetSubmeshes() -> decltype(m_submeshes) &;

        /// @brief Materialize a runtime mesh component from a mesh asset
        virtual void Init() override;
        virtual void Tick(float dt) override;

        std::shared_ptr<MeshAsset> m_mesh_asset;
    };
}

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
