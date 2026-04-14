#ifndef RENDER_RESOURCE_STATICMESHRENDERERPROVIDER_INCLUDED
#define RENDER_RESOURCE_STATICMESHRENDERERPROVIDER_INCLUDED

#include "IRenderResourceProvider.h"
#include "StaticMeshResource.h"

#include <unordered_map>

namespace Engine {
    namespace RenderSystemState {
        class StaticMeshRendererProvider final : public IRenderResourceProvider {
            std::unordered_map<GUID, uint32_t> m_records{};

        public:
            std::type_index GetTypeID() const noexcept override;
            RenderResourceHandle Acquire(RenderResourceManager &manager, RenderSystem &system, GUID guid) override;
            void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept override;
            bool EnsureReady(RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle) override;
            void OnRecordDestroy(GUID guid) noexcept override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_STATICMESHRENDERERPROVIDER_INCLUDED
