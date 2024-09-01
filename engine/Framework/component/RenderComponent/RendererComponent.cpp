#include "RendererComponent.h"
#include "Render/Material/Material.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
#include "Framework/go/GameObject.h"

namespace Engine
{
    RendererComponent::RendererComponent(
        std::weak_ptr<GameObject> gameObject, 
        std::weak_ptr<RenderSystem> system
    ) : Component(gameObject), m_system(system) {
    }

    RendererComponent::~RendererComponent() {}

    Transform RendererComponent::GetWorldTransform() const
    {
        auto parentGameObject = m_parentGameObject.lock();
        assert(parentGameObject && "A renderer component has no parent game object.");
        return parentGameObject->GetWorldTransform();
    }

    void RendererComponent::Tick(float dt)
    {
    }
    std::shared_ptr<Material> RendererComponent::GetMaterial(uint32_t slot) const {
        assert(slot < m_materials.size());
        return m_materials[slot];
    }
    auto RendererComponent::GetMaterials() -> decltype(m_materials)& {
        return m_materials;
    }
} // namespace Engine
