#include "Transform.h"

namespace Engine
{
    glm::mat4 Transform::GetModelMatrix() const
    {
        glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), m_position);
        modelMatrix = modelMatrix * glm::mat4_cast(m_rotation);
        modelMatrix = glm::scale(modelMatrix, m_scale);
        return modelMatrix;
    }

    Transform Transform::operator*(const Transform &other) const
    {
        Transform result{
            m_position + m_rotation * m_scale * other.m_position,
            m_rotation * other.m_rotation,
            m_scale * other.m_scale
        };
        return result;
    }

    Transform & Transform::SetPosition(glm::vec3 position)
    {
        m_position = position;
        return *this;
    }

    Transform & Transform::SetRotationEuler(glm::vec3 euler)
    {
        m_rotation = glm::quat(euler);
        return *this;
    }

    Transform & Transform::SetRotation(glm::quat quat)
    {
        m_rotation = quat;
        return *this;
    }

    Transform & Transform::SetRotationAxisAngles(glm::vec3 axisAngles)
    {
         m_rotation = glm::angleAxis(glm::radians(axisAngles.z), glm::vec3(0, 0, 1))
                * glm::angleAxis(glm::radians(axisAngles.y), glm::vec3(0, 1, 0))
                * glm::angleAxis(glm::radians(axisAngles.x), glm::vec3(1, 0, 0));
        return *this;
    }

    Transform & Transform::SetScale(glm::vec3 scale)
    {
        m_scale = scale;
        return *this;
    }

    const glm::vec3 &Transform::GetPosition() const
    {
        return m_position;
    }

    glm::vec3 Transform::GetEulerAngles() const
    {
        return glm::eulerAngles(m_rotation);
    }

    const glm::quat &Transform::GetQuat() const
    {
        return m_rotation;
    }

    glm::vec3 Transform::GetAxisAngles() const
    {
        glm::vec3 axis = glm::axis(m_rotation);
        float angle = glm::degrees(glm::angle(m_rotation));
        return axis * angle;
    }

    const glm::vec3 &Transform::GetScale() const
    {
        return m_scale;
    }
} // namespace Engine
