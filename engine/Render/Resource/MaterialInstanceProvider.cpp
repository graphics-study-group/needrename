#include "MaterialInstanceProvider.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialAsset.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"

#include <cassert>

namespace Engine::RenderSystemState {
    namespace {
        MaterialAsset *ResolveMaterialAsset(AssetRef &ref, bool async_load) {
            auto *asset = ref.as<MaterialAsset>(async_load);
            if (asset || !async_load) return asset;

            ref.Acquire();
            return ref.as<MaterialAsset>(false);
        }
    } // namespace

    std::type_index MaterialInstanceProvider::GetTypeID() const noexcept {
        return typeid(MaterialInstance *);
    }

    RenderResourceHandle MaterialInstanceProvider::Acquire(
        RenderResourceManager &manager, RenderSystem &system, GUID guid
    ) {
        auto handle = manager.TryReuseRecordByGUID(GetTypeID(), guid);
        if (handle.IsValid()) return handle;

        AssetRef mat_ref(guid);
        auto *mat_asset = ResolveMaterialAsset(mat_ref, false);
        assert(mat_asset);

        auto library_handle = manager.Acquire<MaterialLibrary>(mat_asset->m_library.GetGUID());
        auto *library = manager.Resolve<MaterialLibrary>(library_handle);
        assert(library);

        auto instance = std::make_shared<MaterialInstance>(system, *library);
        instance->Instantiate(*mat_asset);

        handle = manager.CreateRecord(GetTypeID(), guid, instance);
        m_dependencies[handle.index] = {library_handle};
        return handle;
    }

    RenderResourceHandle MaterialInstanceProvider::AcquireAsync(
        RenderResourceManager &manager, RenderSystem &system, GUID guid
    ) {
        auto handle = manager.TryReuseRecordByGUID(GetTypeID(), guid);
        if (handle.IsValid()) return handle;

        AssetRef mat_ref(guid);
        auto *mat_asset = ResolveMaterialAsset(mat_ref, true);
        // TODO: support a true pending async MaterialInstance resource instead
        // of falling back to a forced synchronous load here.
        assert(mat_asset);

        auto library_handle = manager.AcquireAsync<MaterialLibrary>(mat_asset->m_library.GetGUID());
        auto *library = manager.Resolve<MaterialLibrary>(library_handle);
        assert(library);

        auto instance = std::make_shared<MaterialInstance>(system, *library);
        instance->Instantiate(*mat_asset);

        handle = manager.CreateRecord(GetTypeID(), guid, instance);
        m_dependencies[handle.index] = {library_handle};
        return handle;
    }

    void *MaterialInstanceProvider::Resolve(
        RenderResourceManager &manager, RenderResourceHandle handle
    ) const noexcept {
        return manager.ResolvePayload(handle, GetTypeID());
    }

    bool MaterialInstanceProvider::IsReady(
        RenderResourceManager &manager, RenderSystem &, RenderResourceHandle handle
    ) const noexcept {
        return Resolve(manager, handle) != nullptr;
    }

    void MaterialInstanceProvider::EnsureReady(
        RenderResourceManager &manager, RenderSystem &, RenderResourceHandle handle
    ) {
        (void)Resolve(manager, handle);
    }

    void MaterialInstanceProvider::OnRecordDestroy(
        RenderResourceManager &manager, RenderResourceHandle handle
    ) noexcept {
        auto it = m_dependencies.find(handle.index);
        if (it == m_dependencies.end()) return;

        for (auto dep : it->second) {
            manager.Release(dep);
        }
        m_dependencies.erase(it);
    }
} // namespace Engine::RenderSystemState
