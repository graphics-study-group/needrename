#include "Render/Resource/BuiltInResourceProviders.h"

#include "Asset/Material/MaterialAsset.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/Resource/StaticMeshResource.h"

#include <cassert>

namespace Engine::RenderSystemState {
    const std::type_info &MaterialLibraryProvider::GetResourceType() const noexcept {
        return typeid(MaterialLibrary);
    }

    std::shared_ptr<void> MaterialLibraryProvider::Create(
        RenderSystem &system, RenderResourceHub &, uint64_t, const GUID &guid
    ) {
        AssetRef library_ref(guid);
        auto *asset = library_ref.as<MaterialLibraryAsset>();
        assert(asset);

        auto library = std::make_shared<MaterialLibrary>(system);
        library->Instantiate(*asset);
        return library;
    }

    const char *MaterialLibraryProvider::GetDebugName() const noexcept {
        return "MaterialLibraryProvider";
    }

    const std::type_info &MaterialInstanceProvider::GetResourceType() const noexcept {
        return typeid(MaterialInstance);
    }

    std::shared_ptr<void> MaterialInstanceProvider::Create(
        RenderSystem &system, RenderResourceHub &hub, uint64_t dependency_owner, const GUID &guid
    ) {
        AssetRef material_ref(guid);
        auto *material_asset = material_ref.as<MaterialAsset>();
        assert(material_asset);

        auto library = hub.Acquire<MaterialLibrary>(material_asset->m_library.GetGUID(), dependency_owner);
        auto instance = std::make_shared<MaterialInstance>(system, *library);
        instance->Instantiate(*material_asset);
        return instance;
    }

    void MaterialInstanceProvider::Destroy(
        RenderSystem &, RenderResourceHub &hub, uint64_t dependency_owner, const GUID &, std::shared_ptr<void> &
    ) {
        hub.ReleaseOwner(dependency_owner);
    }

    const char *MaterialInstanceProvider::GetDebugName() const noexcept {
        return "MaterialInstanceProvider";
    }

    const std::type_info &StaticMeshProvider::GetResourceType() const noexcept {
        return typeid(StaticMeshResource);
    }

    std::shared_ptr<void> StaticMeshProvider::Create(RenderSystem &, RenderResourceHub &, uint64_t, const GUID &guid) {
        return std::make_shared<StaticMeshResource>(guid);
    }

    const char *StaticMeshProvider::GetDebugName() const noexcept {
        return "StaticMeshProvider";
    }
} // namespace Engine::RenderSystemState
