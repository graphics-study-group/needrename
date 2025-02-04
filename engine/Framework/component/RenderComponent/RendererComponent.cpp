#include "RendererComponent.h"
#include "Render/RenderSystem.h"
#include <Render/Material/Templates/BlinnPhong.h>
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

        // TODO: Implement materialize from material assets. Load shader from material assets. Construct a universal material class for rendering. Unimplemented due to we haven't implemented a universal material class.
        // XXX: No universal material class is implemented. We use BlinnPhong as a placeholder.
        for (size_t i = 0; i < m_material_assets.size(); i++)
        {
            auto ptr = std::make_shared<Materials::BlinnPhongInstance>(m_system, system->GetMaterialRegistry().GetMaterial("Built-in Blinn-Phong"));
            m_materials.push_back(ptr);
        }

        auto &tcb = system->GetTransferCommandBuffer();
        tcb.Begin();
        for (size_t i = 0; i < m_material_assets.size(); i++)
        {
            auto mat_ptr = std::dynamic_pointer_cast<Materials::BlinnPhongInstance>(m_materials[i]);
            assert(mat_ptr);
            mat_ptr->Convert(m_material_assets[i], tcb);
        }
        tcb.End();
        tcb.SubmitAndExecute();
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
