#ifndef RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
#define RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED

#include "IRenderResourceProvider.h"

#include <unordered_map>

namespace Engine::RenderSystemState {
    class MaterialLibraryProvider final : public IRenderResourceProvider {
        std::unordered_map<GUID, uint32_t> m_records{};

    public:
        std::type_index GetTypeID() const noexcept override;
        RenderResourceHandle Acquire(
            RenderResourceManager &manager,
            RenderSystem &system,
            GUID guid,
            const RenderResourceAcquireContext &context
        ) override;
        void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept override;
        bool EnsureReady(RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle) override;
        void OnRecordDestroy(GUID guid, uint32_t submesh_index) noexcept override;
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RESOURCE_MATERIALLIBRARYPROVIDER_INCLUDED
