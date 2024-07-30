#ifndef FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED

#include <glm.hpp>
#include "Core/Math/Transform.h"
#include "Framework/component/Component.h"

namespace Engine
{
    class Component;

    class TransformComponent : public Component
    {
    public:
        TransformComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~TransformComponent();

        void Tick(float dt) override {}

        void SetPosition(glm::vec3 position) { m_transform.m_position = position; }
        void SetRotation(glm::vec3 rotation) { m_transform.m_rotation = rotation; }
        void SetScale(glm::vec3 scale) { m_transform.m_scale = scale; }
        void SetTransform(const Transform& transform) { m_transform = transform; }

        const glm::vec3& GetPosition() const { return m_transform.m_position; }
        const glm::vec3& GetRotation() const { return m_transform.m_rotation; }
        const glm::vec3& GetScale() const { return m_transform.m_scale; }
        const Transform& GetTransform() const { return m_transform; }

        const Transform& GetTransform() { return m_transform; }
        Transform GetWorldTransform() const;

    protected:
        Transform m_transform;
    };
}

#endif // FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
