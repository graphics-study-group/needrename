#include "MaterialRegistry.h"

#include "Render/Material/Templates/BlinnPhong.h"

namespace Engine::RenderSystemState {
    void MaterialRegistry::Create(std::weak_ptr<RenderSystem> system)
    {
        this->m_system = system;
    }

    void MaterialRegistry::AddMaterial(std::shared_ptr<AssetRef> ref)
    {
        auto asset = ref->cas<MaterialTemplateAsset>();
        assert(asset);
        this->operator[](asset->name) = std::make_shared<MaterialTemplate>(m_system, ref);
    }
    auto MaterialRegistry::GetMaterial(const std::string &name) -> decltype(this->at(name))
    {
        return this->at(name);
    }
}
