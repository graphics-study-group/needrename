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

            virtual void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept = 0;

            /**
             * @brief Ensure the resource is consumable by the render path.
             *
             * Some resource types, such as StaticMeshResource, perform a true GPU upload or residency step here. 
             */
            virtual bool EnsureReady(
                RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
            ) = 0;

            virtual void OnRecordDestroy(GUID guid) noexcept = 0;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
