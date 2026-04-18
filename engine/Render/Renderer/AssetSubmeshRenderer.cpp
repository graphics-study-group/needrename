#include "AssetSubmeshRenderer.h"

#include "Asset/AssetRef.h"
#include "Render/RenderSystem.h"
#include "Render/Resource/StaticMeshResource.h"

#include <cassert>

namespace Engine {
    AssetSubmeshRenderer::AssetSubmeshRenderer(
        uint32_t layer,
        bool cast_shadow,
        bool eagerly_loaded,
        RenderSystemState::RenderResourceHandle mesh_resource,
        RenderSystemState::RenderResourceHandle material_resource,
        uint32_t submesh_index,
        StaticMeshResource *mesh
    ) :
        RuntimeRenderer(layer, cast_shadow, eagerly_loaded), m_submesh_index(submesh_index), m_mesh(mesh),
        m_mesh_resource(mesh_resource) {
        SetResourceHandle(MESH_RESOURCE_SLOT, mesh_resource);
        SetResourceHandle(MATERIAL_RESOURCE_SLOT, material_resource);
    }

    AssetSubmeshRenderer::AssetSubmeshRenderer(
        uint32_t submesh_index, StaticMeshResource *mesh, uint32_t layer, bool cast_shadow
    ) : RuntimeRenderer(layer, cast_shadow, false), m_submesh_index(submesh_index), m_mesh(mesh) {
    }

    std::unique_ptr<AssetSubmeshRenderer> AssetSubmeshRenderer::Create(
        RenderSystem &system,
        const AssetRef &mesh_asset_ref,
        const AssetRef &material_asset_ref,
        uint32_t submesh_index,
        uint32_t layer,
        bool cast_shadow,
        bool eagerly_loaded
    ) {
        auto &resource_manager = system.GetRenderResourceManager();

        auto mesh_resource = eagerly_loaded
                                 ? resource_manager.Acquire<StaticMeshResource>(mesh_asset_ref.GetGUID())
                                 : resource_manager.AcquireAsync<StaticMeshResource>(mesh_asset_ref.GetGUID());
        auto *mesh = resource_manager.Resolve<StaticMeshResource>(mesh_resource);
        assert(mesh);

        if (eagerly_loaded) {
            resource_manager.EnsureReady<StaticMeshResource>(mesh_resource);
        }

        auto material_resource = eagerly_loaded
                                     ? resource_manager.Acquire<MaterialInstance>(material_asset_ref.GetGUID())
                                     : resource_manager.AcquireAsync<MaterialInstance>(material_asset_ref.GetGUID());

        return std::unique_ptr<AssetSubmeshRenderer>(new AssetSubmeshRenderer(
            layer, cast_shadow, eagerly_loaded, mesh_resource, material_resource, submesh_index, mesh
        ));
    }

    AssetSubmeshRenderer::~AssetSubmeshRenderer() noexcept = default;

    bool AssetSubmeshRenderer::IsReady() const noexcept {
        return m_mesh && m_mesh->IsSubmeshReady(m_submesh_index);
    }

    uint32_t AssetSubmeshRenderer::GetIndexCount() const noexcept {
        const auto &submesh = m_mesh->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return submesh.index_count;
    }

    uint32_t AssetSubmeshRenderer::GetVertexAttributeCount() const noexcept {
        const auto &submesh = m_mesh->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return submesh.vertex_attribute_count;
    }

    VertexAttribute AssetSubmeshRenderer::GetVertexAttributeFormat() const noexcept {
        const auto &submesh = m_mesh->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return submesh.attributes;
    }

    void AssetSubmeshRenderer::FillVertexAttributeBufferBindings(
        std::vector<BufferBindingInfo> &bindings
    ) const noexcept {
        const auto &submesh_ref = m_mesh->GetSubmeshData(m_submesh_index);
        assert(submesh_ref.vi_buffer);
        bindings.reserve(submesh_ref.attribute_offsets.size());
        for (auto offset : submesh_ref.attribute_offsets) {
            bindings.push_back({submesh_ref.vi_buffer.get(), offset, 0});
        }
        bindings.pop_back();
    }

    RuntimeRenderer::BufferBindingInfo AssetSubmeshRenderer::GetIndexBufferBinding() const noexcept {
        const auto &submesh = m_mesh->GetSubmeshData(m_submesh_index);
        assert(submesh.vi_buffer);
        return {submesh.vi_buffer.get(), submesh.attribute_offsets.back(), 0};
    }

    bool AssetSubmeshRenderer::IsResourcesReady(
        RenderSystemState::RenderResourceManager &resource_manager
    ) const noexcept {
        if (!m_mesh) return false;
        if (!m_mesh_resource.IsValid()) return IsReady();
        if (!resource_manager.IsReady<StaticMeshResource>(m_mesh_resource)) return false;
        return IsReady();
    }
} // namespace Engine
