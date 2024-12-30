#include "RendererComponent.h"
#include <Render/Material/Material.h>
#include <Render/Material/BlinnPhong.h>
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
        m_system.lock()->RegisterComponent(std::dynamic_pointer_cast<RendererComponent>(shared_from_this()));

        // TODO: Implement materialize from material assets. Load shader from material assets. Construct a universal material class for rendering. Unimplemented due to we haven't implemented a universal material class.
        // XXX: No universal material class is implemented. We use BlinnPhong as a placeholder.
        for (size_t i = 0; i < m_material_assets.size(); i++)
        {
            auto ptr = std::make_shared<BlinnPhong>(m_system, m_material_assets[i]);
            m_materials.push_back(ptr);
        }

        auto &tcb = m_system.lock()->GetTransferCommandBuffer();
        tcb.Begin();
        for (auto &material : m_materials)
        {
            std::shared_ptr<BlinnPhong> mat_ptr = std::dynamic_pointer_cast<BlinnPhong>(material);
            assert(mat_ptr);
            mat_ptr->CommitBuffer(tcb);
        }
        tcb.End();
        tcb.SubmitAndExecute();
    }

    void RendererComponent::Tick(float dt)
    {
    }

    std::shared_ptr<Material> RendererComponent::GetMaterial(uint32_t slot) const
    {
        assert(slot < m_materials.size());
        return m_materials[slot];
    }

    auto RendererComponent::GetMaterials() -> decltype(m_materials) &
    {
        return m_materials;
    }
} // namespace Engine
