#include "MaterialLibraryManager.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"

#include <cassert>

namespace Engine::RenderSystemState {
    MaterialLibraryHandle MaterialLibraryManager::CreateFromAssetImpl(GUID guid, uint32_t deallocate_after_frames) {
        AssetRef ref(guid);
        // MaterialLibrary always load eagerly
        ref.Acquire();
        auto *asset = ref.as<MaterialLibraryAsset>(false);
        assert(asset);
        auto library = std::make_unique<MaterialLibrary>(m_system);
        library->Instantiate(*asset);
        return Create(std::move(library), deallocate_after_frames);
        ;
    }

    void MaterialLibraryManager::AcquireImpl(MaterialLibraryHandle &) {
        // MaterialLibrary is always loaded eagerly in CreateFromAssetImpl; no action needed.
    }

    void MaterialLibraryManager::AcquireAsyncImpl(MaterialLibraryHandle &) {
        // MaterialLibrary is always loaded eagerly in CreateFromAssetImpl; no action needed.
    }

    void MaterialLibraryManager::ReleaseImpl(MaterialLibraryHandle &) {
        // No-op since refcounting and deallocation is handled by the base manager logic.
    }

    bool MaterialLibraryManager::IsReadyImpl(const MaterialLibraryHandle &) const noexcept {
        // MaterialLibrary is always loaded eagerly in CreateFromAssetImpl, so if the handle is valid, we consider it ready.
        return true;
    }

    void MaterialLibraryManager::EnsureReadyImpl(MaterialLibraryHandle &) {
        // MaterialLibrary is always loaded eagerly in CreateFromAssetImpl, so if the handle is valid, we consider it ready. No additional action is needed here.
    }

    void MaterialLibraryManager::OnDestroyImpl(MaterialLibraryHandle &) noexcept {
        // dependencies will be released in ~MaterialLibrary(), so no need to do anything here.
    }
} // namespace Engine::RenderSystemState
