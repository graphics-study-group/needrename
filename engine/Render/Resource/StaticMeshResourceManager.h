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
     * Purpose and lifecycle:
     * - GUID maps to a MeshAsset GUID (polygon data, vertex attributes, submesh info).
     * - Payload is a StaticMeshResource object that owns GPU-side vertex/index buffers
     *   for all submeshes.
     * - StaticMeshResource implements the IAsynchPrepared interface; GPU submission is
     *   intentionally deferred until Acquire*Impl triggers it.
     *
     * Preparation model (lazy GPU submission):
     * - CreateFromAssetImpl creates the StaticMeshResource object but does NOT submit
     *   data to GPU; this defers expensive buffer allocation until the resource is
     *   actually needed (acquire time).
     * - AcquireImpl (sync path): Calls EnsureReady, forcing immediate GPU submission.
     * - AcquireAsyncImpl (async path): Currently falls back to EnsureReady (TODO: implement
     *   true async submission without blocking).
     * - IsReadyImpl queries StaticMeshResource::IsReady(), which checks whether all
     *   submesh GPU buffers exist.
     * - EnsureReadyImpl calls StaticMeshResource::Submit() if not yet ready, which
     *   allocates GPU buffers and enqueues copy operations via SubmissionHelper.
     *
     * GPU resource ownership:
     * - Each submesh's vertex/index buffer (DeviceBuffer) is owned by StaticMeshResource.
     * - OnDestroyImpl calls StaticMeshResource::Remove(), which resets buffer unique_ptrs,
     *   triggering RAII cleanup of the underlying GPU allocations.
     *
     * Use case and design intent:
     * - This is the primary resource for mesh rendering; synchronous acquire ensures
     *   GPU readiness before first use.
     * - Deferred submission reduces startup cost when creating many meshes but only
     *   using some.
     */
    class StaticMeshResourceManager final : public IRenderResourceManager<StaticMeshResource> {
    public:
        using IRenderResourceManager<StaticMeshResource>::IRenderResourceManager;

        /**
         * @brief Create a StaticMeshResource record for the given mesh asset GUID.
         *
         * Creates StaticMeshResource object with the given asset GUID.
         * Does NOT submit GPU data; submission is deferred to AcquireImpl/AcquireAsyncImpl.
         * Returns a handle with refcount=1.
         *
         * @param guid GUID of the MeshAsset to load.
         * @param deallocate_after_frames Frame countdown before deferred destruction.
         * @return Newly allocated StaticMeshResourceHandle.
         */
        StaticMeshResourceHandle CreateFromAssetImpl(GUID guid, uint32_t deallocate_after_frames);

        /**
         * @brief Synchronous acquire: ensure GPU submission is complete before returning.
         *
         * Calls EnsureReady, which triggers StaticMeshResource::Submit() if needed.
         * Blocks until all submesh GPU buffers are allocated and uploaded.
         * Increments refcount in base class Acquire().
         *
         * @param handle Target handle.
         */
        void AcquireImpl(StaticMeshResourceHandle &handle);

        /**
         * @brief Asynchronous acquire: request GPU submission via async path.
         *
         * Currently implemented as fallback to EnsureReady (synchronous).
         * TODO: Implement true async submission that enqueues GPU work without blocking.
         * Increments refcount in base class AcquireAsync().
         *
         * @param handle Target handle.
         */
        void AcquireAsyncImpl(StaticMeshResourceHandle &handle);

        /**
         * @brief Release (no-op).
         *
         * Deferred reclamation countdown is managed entirely by base class TickFrame logic.
         */
        void ReleaseImpl(StaticMeshResourceHandle &handle);

        /**
         * @brief Check whether all submesh GPU buffers are ready.
         *
         * Queries the underlying resource's readiness state.
         * Returns false if any submesh GPU buffer is not yet allocated.
         *
         * @param handle Target handle.
         * @return True if handle is valid and StaticMeshResource::IsReady() is true.
         */
        bool IsReadyImpl(const StaticMeshResourceHandle &handle) const noexcept;

        /**
         * @brief Ensure all submesh GPU buffers are ready.
         *
         * If StaticMeshResource::IsReady() is false, calls Submit() to allocate and
         * upload all submesh GPU buffers.
         * Enqueues copy operations via SubmissionHelper; actual GPU work may be
         * deferred to later in the frame.
         *
         * @param handle Target handle.
         */
        void EnsureReadyImpl(StaticMeshResourceHandle &handle);

        /**
         * @brief Cleanup upon final destruction.
         *
         * Calls StaticMeshResource::Remove(), which:
         * - Resets each submesh's vi_buffer unique_ptr, triggering GPU buffer cleanup.
         * - Clears internal metadata (attribute offsets, vertex counts, etc.).
         * - Releases the mesh asset reference if held.
         *
         * @param handle Target handle.
         */
        void OnDestroyImpl(StaticMeshResourceHandle &handle) noexcept;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_STATICMESHRESOURCEPROVIDER_INCLUDED
