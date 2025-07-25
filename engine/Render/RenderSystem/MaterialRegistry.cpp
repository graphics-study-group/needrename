#include "MaterialRegistry.h"

#include "Render/Pipeline/Material/Templates/BlinnPhong.h"

namespace Engine::RenderSystemState {
    void MaterialRegistry::Create(std::weak_ptr<RenderSystem> system) {
        this->m_system = system;
    }

    void MaterialRegistry::AddMaterial(std::shared_ptr<AssetRef> ref) {
        auto asset = ref->cas<MaterialTemplateAsset>();
        assert(asset);
        if (this->find(asset->name) == this->end()) {
            auto ptr = std::make_shared<MaterialTemplate>(*(m_system.lock()));
            this->operator[](asset->name) = ptr;
            ptr->InstantiateFromRef(ref);
        }
    }
    auto MaterialRegistry::GetMaterial(const std::string &name) -> decltype(this->at(name)) {
        return this->at(name);
    }
} // namespace Engine::RenderSystemState
