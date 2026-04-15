#ifndef RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED

#include "RenderResourceManager.h"

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        /**
         * @brief Pluggable provider interface for one render resource type.
         */
        class IRenderResourceProvider {
        public:
            virtual ~IRenderResourceProvider() noexcept = default;

            virtual std::type_index GetTypeID() const noexcept = 0;
            virtual RenderResourceHandle Acquire(RenderResourceManager &manager, RenderSystem &system, GUID guid) = 0;
            virtual RenderResourceHandle AcquireAsync(
                RenderResourceManager &manager, RenderSystem &system, GUID guid
            ) = 0;

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

            virtual void OnRecordDestroy(GUID guid) noexcept = 0;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
