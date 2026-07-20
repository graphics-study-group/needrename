#include "RenderResourceHandle.h"

#include "IRenderResourceManager.h"
#include "MainClass.h"
#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState {

    template <typename ResourceType>
    RenderResourceHandle<ResourceType>::~RenderResourceHandle() {
        if (!is_acquired) return;

        auto cmc = MainClass::GetInstance();
        if (!cmc) return;

        auto rs = cmc->GetRenderSystem();
        if (!rs) return;

        auto *mgr = rs->GetRenderResourceManager<typename ResourceTraits<ResourceType>::ManagerType>();
        if (!mgr) return;

        mgr->Release(static_cast<typename ResourceTraits<ResourceType>::HandleType &>(*this));
    }

    template RenderResourceHandle<MaterialInstance>::~RenderResourceHandle();
    template RenderResourceHandle<MaterialLibrary>::~RenderResourceHandle();
    template RenderResourceHandle<StaticMeshResource>::~RenderResourceHandle();

} // namespace Engine::RenderSystemState
