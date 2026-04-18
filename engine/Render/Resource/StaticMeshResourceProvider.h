#ifndef RENDER_RESOURCE_STATICMESHRESOURCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_STATICMESHRESOURCEPROVIDER_INCLUDED

#include "IRenderResourceProvider.h"
#include "StaticMeshResource.h"

#include <unordered_map>

namespace Engine {
    namespace RenderSystemState {
        /**
         * @brief Provider for StaticMeshResource render resources.
         *
         * GUID maps to mesh asset data; payload is StaticMeshResource that owns
         * GPU-side mesh buffers (possibly prepared later in async flow).
         */
        class StaticMeshResourceProvider final : public IRenderResourceProvider {
            std::unordered_map<GUID, uint32_t> m_records{};

        public:
            /**
             * @brief Provider dispatch key, typeid(StaticMeshResource*).
             */
            std::type_index GetTypeID() const noexcept override;

            /**
             * @brief Synchronously acquire/create and force mesh readiness.
             *
             * This path guarantees GPU buffers are submitted before returning.
             */
            RenderResourceHandle Acquire(RenderResourceManager &manager, RenderSystem &system, GUID guid) override;

            /**
             * @brief Acquire/create mesh resource in async-friendly mode.
             *
             * Payload may be present while GPU preparation is still pending.
             */
            RenderResourceHandle AcquireAsync(RenderResourceManager &manager, RenderSystem &system, GUID guid) override;

            /**
             * @brief Resolve payload pointer as StaticMeshResource.
             */
            void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept override;

            /**
             * @brief Check whether StaticMeshResource is fully ready for draw.
             */
            bool IsReady(
                RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
            ) const noexcept override;

            /**
             * @brief Force immediate mesh submission if not ready.
             */
            void EnsureReady(
                RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle
            ) override;

            /**
             * @brief Forget GUID mapping after record destruction.
             */
            void OnRecordDestroy(GUID guid) noexcept override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_STATICMESHRESOURCEPROVIDER_INCLUDED
