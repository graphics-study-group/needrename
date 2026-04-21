#ifndef RENDER_RESOURCE_STATICMESHRESOURCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_STATICMESHRESOURCEPROVIDER_INCLUDED

#include "IRenderResourceManager.h"

namespace Engine {
    class StaticMeshResource;
}

namespace Engine::RenderSystemState {
    /**
     * @brief Manager for StaticMeshResource render resources.
     *
     * GUID maps to mesh asset data; payload is a StaticMeshResource that owns
     * GPU-side vertex/index buffers. GPU submission is driven by AcquireImpl
     * (sync) or AcquireAsyncImpl (async, best-effort).
     */
    class StaticMeshResourceManager final : public IRenderResourceManager<StaticMeshResource> {
    public:
        using IRenderResourceManager<StaticMeshResource>::IRenderResourceManager;

        /**
         * @brief Create a StaticMeshResource record for the given asset GUID.
         *
         * Only allocates the resource object; GPU submission is left to
         * AcquireImpl / AcquireAsyncImpl so the sync vs. async policy is
         * decided at the call site.
         */
        StaticMeshResourceHandle CreateFromAssetImpl(GUID guid, uint32_t deallocate_after_frames);

        /// @brief Synchronous acquire: forces GPU buffer submission before returning.
        void AcquireImpl(StaticMeshResourceHandle handle);

        /// @brief Async acquire: attempts a non-blocking GPU submission; may defer if asset is not yet ready.
        void AcquireAsyncImpl(StaticMeshResourceHandle handle);

        /// @brief No-op: deferred reclamation countdown is managed by TickFrame.
        void ReleaseImpl(StaticMeshResourceHandle handle);

        /// @brief Returns true when all submesh GPU buffers have been prepared.
        bool IsReadyImpl(StaticMeshResourceHandle handle) const noexcept;

        /// @brief Forces synchronous GPU submission if the resource is not yet ready.
        void EnsureReadyImpl(StaticMeshResourceHandle handle);

        /// @brief GPU buffers are owned by StaticMeshResource and released in its destructor.
        void OnDestroyImpl(StaticMeshResourceHandle handle) noexcept;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_STATICMESHRESOURCEPROVIDER_INCLUDED
