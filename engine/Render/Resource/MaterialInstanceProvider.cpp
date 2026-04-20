#include "MaterialInstanceProvider.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialAsset.h"
#include "MaterialLibraryProvider.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"

#include <cassert>

namespace Engine::RenderSystemState {
    MaterialInstanceProvider::MaterialInstanceProvider(RenderSystem &system) : IRenderResourceProvider(system) {
    }

    MaterialInstanceHandle MaterialInstanceProvider::CreateFromAssetImpl(GUID guid) {
        AssetRef mat_ref(guid);
        // MaterialInstance always load eagerly
        mat_ref.Acquire();
        auto *mat_asset = mat_ref.as<MaterialAsset>(false);
        assert(mat_asset);

        auto library_handle = m_system.GetRenderResourceManager<MaterialLibraryProvider>().CreateOrReuseFromAsset(
            mat_asset->m_library.GetGUID()
        );
        auto *library = m_system.GetRenderResourceManager<MaterialLibraryProvider>().Resolve(library_handle);
        assert(library);

        auto instance = std::make_unique<MaterialInstance>(m_system, library_handle);
        instance->Instantiate(*mat_asset);

        return Create(std::move(instance));
    }

    void MaterialInstanceProvider::AcquireImpl(MaterialInstanceHandle handle) {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so no need to do anything here.
    }

    void MaterialInstanceProvider::AcquireAsyncImpl(MaterialInstanceHandle handle) {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so no need to do anything here.
    }

    void MaterialInstanceProvider::ReleaseImpl(MaterialInstanceHandle handle) {
        // No-op since we don't have reference counting for MaterialInstance payloads. The provider relies on the manager to call OnDestroyImpl when the record is destroyed, which will release the dependency handles.
    }

    bool MaterialInstanceProvider::IsReadyImpl(MaterialInstanceHandle handle) const noexcept {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so if the handle is valid, we consider it ready.
        return true;
    }

    void MaterialInstanceProvider::EnsureReadyImpl(MaterialInstanceHandle handle) {
        // MaterialInstance is always loaded eagerly in CreateFromAssetImpl, so if the handle is valid, we consider it ready. No additional action is needed here.
    }

    void MaterialInstanceProvider::OnDestroyImpl(MaterialInstanceHandle handle) noexcept {
        // dependencies will be released in ~MaterialInstance(), so no need to do anything here.
    }
} // namespace Engine::RenderSystemState
