#include "MaterialLibraryProvider.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"

#include <cassert>

namespace Engine::RenderSystemState {
    namespace {
        MaterialLibraryAsset *ResolveMaterialLibraryAsset(AssetRef &ref, bool async_load) {
            auto *asset = ref.as<MaterialLibraryAsset>(async_load);
            if (asset || !async_load) return asset;

            if (ref.IsAcquired()) {
                ref.Release();
            }
            ref.Acquire();
            return ref.as<MaterialLibraryAsset>(false);
        }
    } // namespace

    MaterialLibraryHandle MaterialLibraryProvider::CreateFromAssetImpl(GUID guid) {
        AssetRef ref(guid);
        auto *asset = ResolveMaterialLibraryAsset(ref, false);
        assert(asset);

        auto library = std::make_unique<MaterialLibrary>(m_system);
        library->Instantiate(*asset);

        auto handle = Create(std::move(library));
        m_guid_to_handle[guid] = handle;
        return handle;
    }

    void MaterialLibraryProvider::AcquireImpl(MaterialLibraryHandle) {
        // MaterialLibrary is always loaded eagerly in CreateFromAssetImpl; no action needed.
    }

    void MaterialLibraryProvider::AcquireAsyncImpl(MaterialLibraryHandle) {
        // TODO: support true async MaterialLibrary loading; falls back to synchronous creation for now.
    }

    void MaterialLibraryProvider::ReleaseImpl(MaterialLibraryHandle) {
        // No-op; deferred reclamation countdown is managed by TickFrame.
    }

    bool MaterialLibraryProvider::IsReadyImpl(MaterialLibraryHandle) const noexcept {
        // MaterialLibrary is always instantiated synchronously; a valid handle implies readiness.
        return true;
    }

    void MaterialLibraryProvider::EnsureReadyImpl(MaterialLibraryHandle) {
        // Library pipelines are still created lazily inside MaterialLibrary::FindMaterialTemplate.
        // Provider readiness guarantees only that the library object itself is instantiated.
    }

    void MaterialLibraryProvider::OnDestroyImpl(MaterialLibraryHandle) noexcept {
        // No provider-owned dependencies to release for MaterialLibrary.
    }
} // namespace Engine::RenderSystemState
