#include "MaterialRegistry.h"

#include "Asset/Material/MaterialTemplateAsset.h"

#include "Render/Pipeline/Material/Templates/BlinnPhong.h"

namespace Engine::RenderSystemState {
    void MaterialRegistry::Create(std::weak_ptr<RenderSystem> system) {
        this->m_system = system;
    }

    void MaterialRegistry::AddMaterial(std::shared_ptr<AssetRef> ref) {
        auto asset = ref->cas<MaterialLibraryAsset>();
        assert(asset);
        if (this->find(asset->m_name) == this->end()) {
            auto ptr = std::make_shared<MaterialLibrary>(*(m_system.lock()));
            this->operator[](asset->m_name) = ptr;
            ptr->Instantiate(*ref->cas<MaterialLibraryAsset>());
        }
    }
    auto MaterialRegistry::GetMaterial(const std::string &name) -> decltype(this->at(name)) {
        return this->at(name);
    }
} // namespace Engine::RenderSystemState
