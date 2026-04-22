#include "StaticMeshResourceManager.h"

#include "Asset/AssetRef.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/FrameManager.h"
#include "StaticMeshResource.h"

#include <cassert>

namespace Engine::RenderSystemState {
    StaticMeshResourceHandle StaticMeshResourceManager::CreateFromAssetImpl(
        GUID guid, uint32_t deallocate_after_frames
    ) {
        auto resource = std::make_unique<StaticMeshResource>(guid);
        return Create(std::move(resource), deallocate_after_frames);
    }

    void StaticMeshResourceManager::AcquireImpl(StaticMeshResourceHandle &handle) {
        EnsureReady(handle);
    }

    void StaticMeshResourceManager::AcquireAsyncImpl(StaticMeshResourceHandle &handle) {
        // TODO: Implement asynchronous GPU submission. For now, we fall back to synchronous submission to ensure correctness.
        EnsureReady(handle);
    }

    void StaticMeshResourceManager::ReleaseImpl(StaticMeshResourceHandle &) {
        // No-op; deferred reclamation countdown is managed by TickFrame.
    }

    bool StaticMeshResourceManager::IsReadyImpl(const StaticMeshResourceHandle &handle) const noexcept {
        if (!IsHandleValid(handle)) return false;
        const auto *resource = m_records[handle.index].payload.get();
        return resource != nullptr && resource->IsReady();
    }

    void StaticMeshResourceManager::EnsureReadyImpl(StaticMeshResourceHandle &handle) {
        auto *resource = Resolve(handle);
        assert(resource && "Payload should never be null for a valid handle");
        if (!resource->IsReady()) {
            resource->Submit(m_system.GetAllocatorState(), m_system.GetFrameManager().GetSubmissionHelper());
        }
    }

    void StaticMeshResourceManager::OnDestroyImpl(StaticMeshResourceHandle &handle) noexcept {
        auto *resource = Resolve(handle);
        if (resource) {
            resource->Remove();
        }
    }
} // namespace Engine::RenderSystemState
