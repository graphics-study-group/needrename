#ifndef RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED

#include "RenderResourceManager.h"

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        /**
         * @brief Pluggable backend for one concrete render resource type.
         *
         * RenderResourceManager routes all typed operations to a matching
         * provider by runtime type id. Each provider is responsible for the
         * full lifecycle of one payload type:
         * - create/reuse records in Acquire / AcquireAsync
         * - resolve payload pointers
         * - report readiness without side effects
         * - synchronously force readiness when requested
         * - clear internal GUID -> record bookkeeping on destruction
         */
        class IRenderResourceProvider {
        public:
            virtual ~IRenderResourceProvider() noexcept = default;

            /**
             * @brief Runtime key used by RenderResourceManager dispatch.
             */
            virtual std::type_index GetTypeID() const noexcept = 0;

            /**
             * @brief Acquire a resource and guarantee synchronous creation path.
             *
             * Implementations should return a reusable handle when possible,
             * otherwise create a new record. This path is expected to perform
             * immediate work needed by provider policy.
             */
            virtual RenderResourceHandle Acquire(RenderResourceManager &manager, RenderSystem &system, GUID guid) = 0;

            /**
             * @brief Acquire a resource using asynchronous-friendly path.
             *
             * Implementations may start async work and return a handle whose
             * payload is not fully ready yet. Callers can poll IsReady or use
             * EnsureReady as a synchronous fallback.
             */
            virtual RenderResourceHandle AcquireAsync(
                RenderResourceManager &manager, RenderSystem &system, GUID guid
            ) = 0;

            /**
             * @brief Resolve raw payload pointer for a typed handle.
             *
             * Returns nullptr when handle is stale, type-mismatched, or record
             * payload is no longer available.
             */
            virtual void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept = 0;

            /**
             * @brief Query whether the resource is ready for immediate consumption.
             *
             * Unlike EnsureReady, this method must not trigger loading or GPU
             * submission side effects. It is intended for polling async-loaded
             * resources and skipping work until they become ready.
             */
            virtual bool IsReady(
                RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
            ) const noexcept = 0;

            /**
             * @brief Ensure the resource is consumable by the render path.
             *
             * This operation is the synchronous fallback that forces all
             * provider-owned prerequisites to become available immediately.
             * For example, a provider may need to force AssetRef to complete a
             * load and then submit GPU uploads before returning.
             */
            virtual void EnsureReady(
                RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
            ) = 0;

            /**
             * @brief Notify provider that a GUID record was destroyed.
             *
             * Providers should use this to remove stale internal lookup state.
             */
            virtual void OnRecordDestroy(GUID guid) noexcept = 0;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
