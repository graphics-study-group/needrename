#include "RendererComponent.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/MaterialRegistry.h"
#include <Render/Pipeline/Material/MaterialInstance.h>
#include <Asset/Material/MaterialAsset.h>
#include <Framework/component/RenderComponent/CameraComponent.h>
#include <Framework/object/GameObject.h>
#include <MainClass.h>

namespace Engine
{
    RendererComponent::RendererComponent(
        std::weak_ptr<GameObject> gameObject) : Component(gameObject)
    {
        m_system = MainClass::GetInstance()->GetRenderSystem();
    }

    Transform RendererComponent::GetWorldTransform() const
    {
        auto parentGameObject = m_parentGameObject.lock();
        assert(parentGameObject && "A renderer component has no parent game object.");
        return parentGameObject->GetWorldTransform();
    }

    void RendererComponent::Init()
    {
        m_system = MainClass::GetInstance()->GetRenderSystem();
        auto system = m_system.lock();
        system->RegisterComponent(std::dynamic_pointer_cast<RendererComponent>(shared_from_this()));

        for (size_t i = 0; i < m_material_assets.size(); i++)
        {
            // XXX: This is a temporary solution: It simply check the m_name in material assets and add it to the registry. We should reconsider the relationship between MaterialRegistry and MaterialTemplateAsset.
            auto tpl = m_material_assets[i]->as<MaterialAsset>()->m_template;
            m_system.lock()->GetMaterialRegistry().AddMaterial(tpl);
            auto ptr = std::make_shared<MaterialInstance>(
                *(m_system.lock()), 
                m_system.lock()->GetMaterialRegistry().GetMaterial(tpl->as<MaterialTemplateAsset>()->name)
            );
            m_materials.push_back(ptr);
        }

        for (size_t i = 0; i < m_material_assets.size(); i++)
        {
            auto mat_ptr = std::dynamic_pointer_cast<MaterialInstance>(m_materials[i]);
            auto mat_asset = (m_material_assets[i]->cas<MaterialAsset>());
            assert(mat_ptr && mat_asset);
            mat_ptr->Instantiate(*mat_asset);
        }
    }

    void RendererComponent::Tick(float)
    {
    }

    std::shared_ptr<MaterialInstance> RendererComponent::GetMaterial(uint32_t slot) const
    {
        assert(slot < m_materials.size());
        return m_materials[slot];
    }

    auto RendererComponent::GetMaterials() -> decltype(m_materials) &
    {
        return m_materials;
    }
} // namespace Engine
