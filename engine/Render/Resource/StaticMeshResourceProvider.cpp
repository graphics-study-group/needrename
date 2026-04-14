#include "StaticMeshResourceProvider.h"

#include "Asset/AssetRef.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/FrameManager.h"

namespace Engine {
    namespace RenderSystemState {
        std::type_index StaticMeshResourceProvider::GetTypeID() const noexcept {
            return typeid(StaticMeshResource *);
        }

        RenderResourceHandle StaticMeshResourceProvider::Acquire(
            RenderResourceManager &manager, RenderSystem &, GUID guid
        ) {
            auto it = m_records.find(guid);
            if (it != m_records.end()) {
                auto handle = manager.TryReuseRecord(GetTypeID(), it->second);
                if (handle.IsValid()) return handle;
                m_records.erase(it);
            }

            auto resource = std::make_shared<StaticMeshResource>(AssetRef(guid));
            auto handle = manager.CreateRecord(GetTypeID(), guid, resource);
            m_records[guid] = handle.index;
            return handle;
        }

        void *StaticMeshResourceProvider::Resolve(
            RenderResourceManager &manager, RenderResourceHandle handle
        ) const noexcept {
            return manager.ResolvePayload(handle, GetTypeID());
        }

        bool StaticMeshResourceProvider::EnsureReady(
            RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
        ) {
            auto *payload = static_cast<StaticMeshResource *>(manager.ResolvePayload(handle, GetTypeID()));
            if (!payload) return false;

            if (!payload->IsReady()) {
                payload->EnsurePrepared(system.GetAllocatorState(), system.GetFrameManager().GetSubmissionHelper());
            }
            return payload->IsReady();
        }

        void StaticMeshResourceProvider::OnRecordDestroy(GUID guid) noexcept {
            m_records.erase(guid);
        }
    } // namespace RenderSystemState
} // namespace Engine