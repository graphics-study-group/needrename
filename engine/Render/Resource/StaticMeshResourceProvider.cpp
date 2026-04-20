#include "StaticMeshResourceProvider.h"

#include "StaticMeshResource.h"
#include "Asset/AssetRef.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/FrameManager.h"

#include <cassert>

namespace Engine::RenderSystemState {
    StaticMeshResourceHandle StaticMeshResourceProvider::CreateFromAssetImpl(GUID guid) {
        auto resource = std::make_unique<StaticMeshResource>(AssetRef(guid));
        auto handle = Create(std::move(resource));
        m_guid_to_handle[guid] = handle;
        return handle;
    }

    void StaticMeshResourceProvider::AcquireImpl(StaticMeshResourceHandle handle) {
        EnsureReadyImpl(handle);
    }

    void StaticMeshResourceProvider::AcquireAsyncImpl(StaticMeshResourceHandle handle) {
        auto *resource = Resolve(handle);
        if (!resource || resource->IsReady()) return;
        // Best-effort async submission; returns false (deferred) if the asset is not yet loaded.
        resource->Submit(
            m_system.GetAllocatorState(),
            m_system.GetFrameManager().GetSubmissionHelper(),
            true
        );
    }

    void StaticMeshResourceProvider::ReleaseImpl(StaticMeshResourceHandle) {
        // No-op; deferred reclamation countdown is managed by TickFrame.
    }

    bool StaticMeshResourceProvider::IsReadyImpl(StaticMeshResourceHandle handle) const noexcept {
        if (!IsHandleValid(handle)) return false;
        const auto *resource = m_records[handle.index].payload.get();
        return resource != nullptr && resource->IsReady();
    }

    void StaticMeshResourceProvider::EnsureReadyImpl(StaticMeshResourceHandle handle) {
        auto *resource = Resolve(handle);
        assert(resource && "Payload should never be null for a valid handle");
        if (!resource->IsReady()) {
            resource->Submit(m_system.GetAllocatorState(), m_system.GetFrameManager().GetSubmissionHelper());
        }
    }

    void StaticMeshResourceProvider::OnDestroyImpl(StaticMeshResourceHandle) noexcept {
        // GPU buffers are owned by StaticMeshResource and released in ~StaticMeshResource().
    }
} // namespace Engine::RenderSystemState
