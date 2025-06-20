#include "MeshComponent.h"

#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Asset/Mesh/MeshAsset.h"
#include <Asset/AssetRef.h>

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

    void MeshComponent::Init()
    {
        RendererComponent::Init();

        // Create submeshes from mesh assets
        assert(m_mesh_asset && m_mesh_asset->IsValid());
        m_submeshes.clear();
        size_t submesh_count = m_mesh_asset->as<MeshAsset>()->GetSubmeshCount();
        for (size_t i = 0; i < submesh_count; i++)
        {
            m_submeshes.push_back(std::make_shared<HomogeneousMesh>(
                m_system, m_mesh_asset, i));
        }

        // Commit vertex buffer to GPU
        for (auto &submesh : m_submeshes)
        {
            submesh->Prepare();
            m_system.lock()->GetFrameManager().GetSubmissionHelper().EnqueueVertexBufferSubmission(*submesh);
        }
    }

    void MeshComponent::Tick()
    {
    }
};
