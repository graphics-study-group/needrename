#include "MaterialInstanceProvider.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialAsset.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"

#include <cassert>

namespace Engine::RenderSystemState {
    std::type_index MaterialInstanceProvider::GetTypeID() const noexcept {
        return typeid(MaterialInstance);
    }

    RenderResourceHandle MaterialInstanceProvider::Acquire(
        RenderResourceManager &manager,
        RenderSystem &system,
        GUID guid,
        const RenderResourceAcquireContext &
    ) {
        auto it = m_records.find(guid);
        if (it != m_records.end()) {
            auto handle = manager.TryReuseRecord(GetTypeID(), it->second);
            if (handle.IsValid()) return handle;
            m_records.erase(it);
        }

        AssetRef mat_ref(guid);
        auto *mat_asset = mat_ref.as<MaterialAsset>();
        assert(mat_asset);

        auto library_handle = manager.Acquire<MaterialLibrary>(mat_asset->m_library.GetGUID());
        auto *library = manager.Resolve<MaterialLibrary>(library_handle);
        assert(library);

        auto instance = std::make_shared<MaterialInstance>(system, *library);
        instance->Instantiate(*mat_asset);

        std::vector<RenderResourceHandle> dependencies;
        dependencies.push_back(library_handle);
        auto handle = manager.CreateRecord(GetTypeID(), guid, 0, instance, std::move(dependencies));
        m_records[guid] = handle.index;
        return handle;
    }

    void *MaterialInstanceProvider::Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept {
        return manager.ResolvePayload(handle, GetTypeID());
    }

    bool MaterialInstanceProvider::EnsureReady(
        RenderResourceManager &manager, RenderSystem &, RenderResourceHandle handle
    ) {
        return Resolve(manager, handle) != nullptr;
    }

    void MaterialInstanceProvider::OnRecordDestroy(GUID guid, uint32_t) noexcept {
        m_records.erase(guid);
    }
} // namespace Engine::RenderSystemState
