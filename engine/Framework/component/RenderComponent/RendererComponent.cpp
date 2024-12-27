#include "RendererComponent.h"
#include <Render/Material/Material.h>
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

    RendererComponent::~RendererComponent() {}

    Transform RendererComponent::GetWorldTransform() const
    {
        auto parentGameObject = m_parentGameObject.lock();
        assert(parentGameObject && "A renderer component has no parent game object.");
        return parentGameObject->GetWorldTransform();
    }

    void RendererComponent::Init()
    {
        // TODO: Implement materialize from material assets. Load shader from material assets. Construct a universal material class for rendering. Unimplemented due to we haven't implemented a universal material class.
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
