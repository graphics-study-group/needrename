#include "StaticMeshRendererProvider.h"

#include "Asset/AssetRef.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/Renderer/IVertexBasedRenderer.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"

#include <cassert>

namespace Engine {
    namespace RenderSystemState {
        namespace {
            struct StaticMeshRendererResource {
                AssetRef mesh_asset_ref{};
                std::shared_ptr<StaticHomogeneousMesh::StaticHMeshSharedDataBlock> shared_data{};
                std::shared_ptr<StaticHomogeneousMesh> renderer{};
            };
        }

        std::type_index StaticMeshRendererProvider::GetTypeID() const noexcept {
            return typeid(IVertexBasedRenderer);
        }

        RenderResourceHandle StaticMeshRendererProvider::Acquire(
            RenderResourceManager &manager,
            RenderSystem &system,
            GUID guid,
            const RenderResourceAcquireContext &context
        ) {
            MeshSubmeshKey key{guid, context.submesh_index};
            auto it = m_records.find(key);
            if (it != m_records.end()) {
                auto handle = manager.TryReuseRecord(GetTypeID(), it->second);
                if (handle.IsValid()) {
                    if (context.eagerly_loaded) {
                        manager.EnsureReady<IVertexBasedRenderer>(handle);
                    }
                    return handle;
                }
                m_records.erase(it);
            }

            auto resource = std::make_shared<StaticMeshRendererResource>();
            resource->mesh_asset_ref = AssetRef(guid);
            auto *mesh_asset = resource->mesh_asset_ref.as<MeshAsset>();
            assert(mesh_asset);

            auto weak_it = m_shared_blocks.find(guid);
            if (weak_it != m_shared_blocks.end()) {
                resource->shared_data = weak_it->second.lock();
            }

            if (!resource->shared_data) {
                resource->shared_data = std::make_shared<StaticHomogeneousMesh::StaticHMeshSharedDataBlock>();
                resource->shared_data->submeshes.resize(mesh_asset->GetSubmeshCount());
                m_shared_blocks[guid] = resource->shared_data;
            }

            resource->renderer =
                std::make_shared<StaticHomogeneousMesh>(context.submesh_index, *mesh_asset, *resource->shared_data);

            auto handle = manager.CreateRecord(GetTypeID(), guid, context.submesh_index, resource);
            m_records[key] = handle.index;
            if (context.eagerly_loaded) {
                manager.EnsureReady<IVertexBasedRenderer>(handle);
            }
            return handle;
        }

        void *StaticMeshRendererProvider::Resolve(
            RenderResourceManager &manager, RenderResourceHandle handle
        ) const noexcept {
            auto *payload = static_cast<StaticMeshRendererResource *>(manager.ResolvePayload(handle, GetTypeID()));
            return payload ? payload->renderer.get() : nullptr;
        }

        bool StaticMeshRendererProvider::EnsureReady(
            RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
        ) {
            auto *payload = static_cast<StaticMeshRendererResource *>(manager.ResolvePayload(handle, GetTypeID()));
            if (!payload || !payload->renderer) return false;

            if (!payload->renderer->IsReady()) {
                payload->renderer->EnsurePrepared(
                    system.GetAllocatorState(), system.GetFrameManager().GetSubmissionHelper()
                );
            }
            return payload->renderer->IsReady();
        }

        void StaticMeshRendererProvider::OnRecordDestroy(GUID guid, uint32_t submesh_index) noexcept {
            m_records.erase(MeshSubmeshKey{guid, submesh_index});
            for (auto it = m_shared_blocks.begin(); it != m_shared_blocks.end();) {
                if (it->second.expired()) {
                    it = m_shared_blocks.erase(it);
                } else {
                    ++it;
                }
            }
        }
    } // namespace RenderSystemState
} // namespace Engine
