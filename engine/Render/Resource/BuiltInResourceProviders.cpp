#include "Render/Resource/BuiltInResourceProviders.h"

#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/MaterialRegistry.h"

namespace Engine::RenderSystemState {
    const std::type_info &MaterialLibraryProvider::GetResourceType() const noexcept {
        return typeid(MaterialLibrary);
    }

    std::shared_ptr<void> MaterialLibraryProvider::Create(RenderSystem &system, const GUID &guid) {
        return system.GetMaterialRegistry().GetOrCreateLibraryShared(guid);
    }

    const char *MaterialLibraryProvider::GetDebugName() const noexcept {
        return "MaterialLibraryProvider";
    }

    const std::type_info &MaterialInstanceProvider::GetResourceType() const noexcept {
        return typeid(MaterialInstance);
    }

    std::shared_ptr<void> MaterialInstanceProvider::Create(RenderSystem &system, const GUID &guid) {
        return system.GetMaterialRegistry().GetOrCreateInstance(guid);
    }

    const char *MaterialInstanceProvider::GetDebugName() const noexcept {
        return "MaterialInstanceProvider";
    }
} // namespace Engine::RenderSystemState
