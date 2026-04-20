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

    std::type_index MaterialLibraryProvider::GetTypeID() const noexcept {
        return typeid(MaterialLibrary *);
    }

    RenderResourceHandle MaterialLibraryProvider::Acquire(
        RenderResourceManager &manager, RenderSystem &system, GUID guid
    ) {
        auto handle = manager.TryReuseRecordByGUID(GetTypeID(), guid);
        if (handle.IsValid()) return handle;

        AssetRef ref(guid);
        auto *asset = ResolveMaterialLibraryAsset(ref, false);
        assert(asset);

        auto library = std::make_shared<MaterialLibrary>(system);
        library->Instantiate(*asset);

        handle = manager.CreateRecord(GetTypeID(), guid, library);
        return handle;
    }

    RenderResourceHandle MaterialLibraryProvider::AcquireAsync(
        RenderResourceManager &manager, RenderSystem &system, GUID guid
    ) {
        auto handle = manager.TryReuseRecordByGUID(GetTypeID(), guid);
        if (handle.IsValid()) return handle;

        AssetRef ref(guid);
        auto *asset = ResolveMaterialLibraryAsset(ref, true);
        // TODO: support a true pending async MaterialLibrary resource instead
        // of falling back to a forced synchronous load here.
        assert(asset);

        auto library = std::make_shared<MaterialLibrary>(system);
        library->Instantiate(*asset);

        handle = manager.CreateRecord(GetTypeID(), guid, library);
        return handle;
    }

    void *MaterialLibraryProvider::Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept {
        return manager.ResolvePayload(handle, GetTypeID());
    }

    bool MaterialLibraryProvider::IsReady(
        RenderResourceManager &manager, RenderSystem &, RenderResourceHandle handle
    ) const noexcept {
        return Resolve(manager, handle) != nullptr;
    }

    void MaterialLibraryProvider::EnsureReady(
        RenderResourceManager &manager, RenderSystem &, RenderResourceHandle handle
    ) {
        // Material library pipelines are still created lazily in
        // MaterialLibrary::FindMaterialTemplate, but provider readiness still
        // guarantees that the library object itself is instantiated now.
        (void)Resolve(manager, handle);
    }

    void MaterialLibraryProvider::OnRecordDestroy(RenderResourceManager &, RenderResourceHandle) noexcept {
    }
} // namespace Engine::RenderSystemState
