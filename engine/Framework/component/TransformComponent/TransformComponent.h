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
        void SetEulerAngles(glm::vec3 euler) { m_transform.m_rotation = glm::quat(euler); }
        void SetQuat(glm::quat quat) { m_transform.m_rotation = quat; }
        void SetAxisAngles(glm::vec3 axisAngles) {
            m_transform.m_rotation = glm::angleAxis(glm::radians(axisAngles.z), glm::vec3(0, 0, 1))
                * glm::angleAxis(glm::radians(axisAngles.y), glm::vec3(0, 1, 0))
                * glm::angleAxis(glm::radians(axisAngles.x), glm::vec3(1, 0, 0));
        }
        void SetScale(glm::vec3 scale) { m_transform.m_scale = scale; }
        void SetTransform(const Transform& transform) { m_transform = transform; }

        const glm::vec3& GetPosition() const { return m_transform.m_position; }
        glm::vec3 GetEulerAngles() const { return glm::eulerAngles(m_transform.m_rotation); }
        const glm::quat& GetQuat() const {return m_transform.m_rotation; }
        glm::vec3 GetAxisAngles() const {
            glm::vec3 axis = glm::axis(m_transform.m_rotation);
            float angle = glm::degrees(glm::angle(m_transform.m_rotation));
            return axis * angle;
        }
        const glm::vec3& GetScale() const { return m_transform.m_scale; }
        const Transform& GetTransform() const { return m_transform; }

        const Transform& GetTransform() { return m_transform; }
        Transform GetWorldTransform() const;

    protected:
        Transform m_transform;
    };
}

#endif // FRAMEWORK_COMPONENT_TRANSFORMCOMPONENT_TRANSFORMCOMPONENT_INCLUDED
