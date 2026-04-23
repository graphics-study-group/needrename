#include "MaterialInstanceManager.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialAsset.h"
#include "MaterialLibraryManager.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"

#include <cassert>

namespace Engine::RenderSystemState {
    MaterialInstanceManager::MaterialInstanceManager(RenderSystem &system) : IRenderResourceManager(system) {
    }

    MaterialInstanceHandle MaterialInstanceManager::CreateFromAssetImpl(GUID guid, uint32_t deallocate_after_frames) {
        AssetRef mat_ref(guid);
        // MaterialInstance always load eagerly
        mat_ref.Acquire();
        auto *mat_asset = mat_ref.as<MaterialAsset>(false);
        assert(mat_asset);

        auto library_handle = m_system.GetRenderResourceManager<MaterialLibraryManager>().CreateOrReuseFromAsset(
            mat_asset->m_library.GetGUID()
        );
        auto *library = m_system.GetRenderResourceManager<MaterialLibraryManager>().Resolve(library_handle);
        assert(library);

        auto instance = std::make_unique<MaterialInstance>(m_system, library_handle);
        instance->Instantiate(*mat_asset);

        return Create(std::move(instance), deallocate_after_frames);
    }

    void MaterialInstanceManager::AcquireImpl(MaterialInstanceHandle &) {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so no need to do anything here.
    }

    void MaterialInstanceManager::AcquireAsyncImpl(MaterialInstanceHandle &) {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so no need to do anything here.
    }

    void MaterialInstanceManager::ReleaseImpl(MaterialInstanceHandle &) {
        // No-op since refcounting and deallocation is handled by the base manager logic.
    }

    bool MaterialInstanceManager::IsReadyImpl(const MaterialInstanceHandle &) const noexcept {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so if the handle is valid, we consider it ready.
        return true;
    }

    void MaterialInstanceManager::EnsureReadyImpl(MaterialInstanceHandle &) {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so if the handle is valid, we consider it ready. No additional action is needed here.
    }

    void MaterialInstanceManager::OnDestroyImpl(MaterialInstanceHandle &) noexcept {
        // dependencies will be released in ~MaterialInstance(), so no need to do anything here.
    }
} // namespace Engine::RenderSystemState
