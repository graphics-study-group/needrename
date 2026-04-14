#include "MaterialLibraryProvider.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"

#include <cassert>

namespace Engine::RenderSystemState {
    std::type_index MaterialLibraryProvider::GetTypeID() const noexcept {
        return typeid(MaterialLibrary);
    }

    RenderResourceHandle MaterialLibraryProvider::Acquire(
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

        AssetRef ref(guid);
        auto *asset = ref.as<MaterialLibraryAsset>();
        assert(asset);

        auto library = std::make_shared<MaterialLibrary>(system);
        library->Instantiate(*asset);

        auto handle = manager.CreateRecord(GetTypeID(), guid, 0, library);
        m_records[guid] = handle.index;
        return handle;
    }

    void *MaterialLibraryProvider::Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept {
        return manager.ResolvePayload(handle, GetTypeID());
    }

    bool MaterialLibraryProvider::EnsureReady(
        RenderResourceManager &manager, RenderSystem &, RenderResourceHandle handle
    ) {
        return Resolve(manager, handle) != nullptr;
    }

    void MaterialLibraryProvider::OnRecordDestroy(GUID guid, uint32_t) noexcept {
        m_records.erase(guid);
    }
} // namespace Engine::RenderSystemState
