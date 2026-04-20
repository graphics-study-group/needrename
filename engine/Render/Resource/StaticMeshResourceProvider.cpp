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
            RenderResourceManager &manager, RenderSystem &system, GUID guid
        ) {
            auto handle = manager.TryReuseRecordByGUID(GetTypeID(), guid);
            if (handle.IsValid()) {
                EnsureReady(manager, system, handle);
                return handle;
            }

            auto resource = std::make_shared<StaticMeshResource>(AssetRef(guid));
            handle = manager.CreateRecord(GetTypeID(), guid, resource);
            EnsureReady(manager, system, handle);
            return handle;
        }

        RenderResourceHandle StaticMeshResourceProvider::AcquireAsync(
            RenderResourceManager &manager, RenderSystem &system, GUID guid
        ) {
            auto handle = manager.TryReuseRecordByGUID(GetTypeID(), guid);
            if (handle.IsValid()) return handle;

            auto resource = std::make_shared<StaticMeshResource>(AssetRef(guid));
            handle = manager.CreateRecord(GetTypeID(), guid, resource);
            // TODO: kick asynchronous GPU upload/background submit once the engine has a dedicated render-resource background job path.
            resource->Submit(system.GetAllocatorState(), system.GetFrameManager().GetSubmissionHelper(), true);
            return handle;
        }

        void *StaticMeshResourceProvider::Resolve(
            RenderResourceManager &manager, RenderResourceHandle handle
        ) const noexcept {
            return manager.ResolvePayload(handle, GetTypeID());
        }

        bool StaticMeshResourceProvider::IsReady(
            RenderResourceManager &manager, RenderSystem &, RenderResourceHandle handle
        ) const noexcept {
            auto *payload = static_cast<StaticMeshResource *>(manager.ResolvePayload(handle, GetTypeID()));
            return payload != nullptr && payload->IsReady();
        }

        void StaticMeshResourceProvider::EnsureReady(
            RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
        ) {
            auto *payload = static_cast<StaticMeshResource *>(manager.ResolvePayload(handle, GetTypeID()));
            assert(payload && "Payload should never be null for a valid handle");

            if (!payload->IsReady()) {
                payload->Submit(system.GetAllocatorState(), system.GetFrameManager().GetSubmissionHelper());
            }
        }

        void StaticMeshResourceProvider::OnRecordDestroy(RenderResourceManager &, RenderResourceHandle) noexcept {
        }
    } // namespace RenderSystemState
} // namespace Engine
