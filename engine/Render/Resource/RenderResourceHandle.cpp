#include "RenderResourceHandle.h"

#include "IRenderResourceManager.h"
#include "Render/RenderRuntime.h"
#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState {

    template <typename ResourceType>
    RenderResourceHandle<ResourceType>::~RenderResourceHandle() {
        if (!is_acquired) return;

        auto *rs = GetRenderRuntime().render_system;
        if (!rs) return;

        auto *mgr = rs->GetRenderResourceManager<typename ResourceTraits<ResourceType>::ManagerType>();
        if (!mgr) return;

        mgr->Release(static_cast<typename ResourceTraits<ResourceType>::HandleType &>(*this));
    }

    template RenderResourceHandle<MaterialInstance>::~RenderResourceHandle();
    template RenderResourceHandle<MaterialLibrary>::~RenderResourceHandle();
    template RenderResourceHandle<StaticMeshResource>::~RenderResourceHandle();

} // namespace Engine::RenderSystemState
