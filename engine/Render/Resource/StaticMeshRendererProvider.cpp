#include "StaticMeshRendererProvider.h"

#include "Asset/AssetRef.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/FrameManager.h"

#include <cassert>

namespace Engine {
    namespace RenderSystemState {
        std::type_index StaticMeshRendererProvider::GetTypeID() const noexcept {
            return typeid(StaticMeshResource *);
        }

        RenderResourceHandle StaticMeshRendererProvider::Acquire(
            RenderResourceManager &manager, RenderSystem &, GUID guid
        ) {
            auto it = m_records.find(guid);
            if (it != m_records.end()) {
                auto handle = manager.TryReuseRecord(GetTypeID(), it->second);
                if (handle.IsValid()) return handle;
                m_records.erase(it);
            }

            AssetRef mesh_asset_ref(guid);
            auto *mesh_asset = mesh_asset_ref.as<MeshAsset>();
            assert(mesh_asset);

            auto resource = std::make_shared<StaticMeshResource>(*mesh_asset);
            auto handle = manager.CreateRecord(GetTypeID(), guid, resource);
            m_records[guid] = handle.index;
            return handle;
        }

        void *StaticMeshRendererProvider::Resolve(
            RenderResourceManager &manager, RenderResourceHandle handle
        ) const noexcept {
            return manager.ResolvePayload(handle, GetTypeID());
        }

        bool StaticMeshRendererProvider::EnsureReady(
            RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
        ) {
            auto *payload = static_cast<StaticMeshResource *>(manager.ResolvePayload(handle, GetTypeID()));
            if (!payload) return false;

            if (!payload->IsReady()) {
                payload->EnsurePrepared(system.GetAllocatorState(), system.GetFrameManager().GetSubmissionHelper());
            }
            return payload->IsReady();
        }

        void StaticMeshRendererProvider::OnRecordDestroy(GUID guid) noexcept {
            m_records.erase(guid);
        }
    } // namespace RenderSystemState
} // namespace Engine
