#include "MaterialRegistry.h"

#include "Render/Material/Templates/BlinnPhong.h"

namespace Engine::RenderSystemState {
    void MaterialRegistry::Create(std::weak_ptr<RenderSystem> system)
    {
        this->m_system = system;
        this->AddDefaultMaterials();
    }

    void MaterialRegistry::AddDefaultMaterials()
    {
        auto bpasset = std::make_shared<Materials::BlinnPhongTemplateAsset>();
        auto bpasset_ref = std::make_shared<AssetRef>(bpasset);
        this->operator[]("Built-in Blinn-Phong") = std::make_shared<Materials::BlinnPhongTemplate>(m_system, bpasset_ref);
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
