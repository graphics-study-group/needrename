#include "MeshComponent.h"

#include "Render/Renderer/HomogeneousMesh.h"
#include "Asset/Mesh/MeshAsset.h"

namespace Engine
{
    MeshComponent::MeshComponent(
        std::weak_ptr<GameObject> gameObject) : RendererComponent(gameObject)
    {
    }

    std::shared_ptr<HomogeneousMesh> MeshComponent::GetSubmesh(uint32_t slot) const
    {
        assert(slot < m_submeshes.size());
        return m_submeshes[slot];
    }
    auto MeshComponent::GetSubmeshes() -> decltype(m_submeshes) &
    {
        return m_submeshes;
    }

    void MeshComponent::Materialize()
    {
        assert(m_mesh_asset && m_mesh_asset->IsValid());

        m_submeshes.clear();
        size_t submesh_count = m_mesh_asset->GetSubmeshCount();
        for (size_t i = 0; i < submesh_count; i++)
        {
            m_submeshes.push_back(std::make_shared<HomogeneousMesh>(
                m_system, m_mesh_asset, i));
        }
    }

    void MeshComponent::Tick(float dt)
    {
    }
};
