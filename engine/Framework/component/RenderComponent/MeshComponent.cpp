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
    void MeshComponent::Materialize(const MeshAsset &asset)
    {
        // Simply load all vertices into one submesh for testing
        // XXX: full implementation
        m_submeshes.clear();
        m_submeshes.push_back(std::make_shared<HomogeneousMesh>(m_system));

        std::vector<VertexStruct::VertexPosition> positions;
        std::vector<VertexStruct::VertexAttribute> attributes;
        std::vector<uint32_t> indices;
        const auto &pos_index = asset.GetTriangle_vert_ids();
        // Load uv as color for testing
        const auto &color_index = asset.GetTriangle_uv_ids();
        const auto &pos = asset.GetPositions();
        const auto &uv = asset.GetUVs();

        assert(pos_index.size() % 3 == 0);
        assert(pos_index.size() == color_index.size());

        for (size_t i = 0; i < pos_index.size(); i++)
        {
            positions.push_back({pos[3 * pos_index[i] + 0], pos[3 * pos_index[i] + 1], pos[3 * pos_index[i] + 2]});
            attributes.push_back({.color = {uv[2 * color_index[i] + 0], uv[2 * color_index[i] + 1], 0.0f},
                                  .normal = {0.0f, 0.0f, 0.0f},
                                  .texcoord1 = {0.0f, 0.0f}});
            // No deduplication for quick and dirty test
            indices.push_back(i);
        }

        auto &mesh = *(m_submeshes[0]);
        mesh.SetPositions(positions);
        mesh.SetAttributes(attributes);
        mesh.SetIndices(indices);
    }
};
