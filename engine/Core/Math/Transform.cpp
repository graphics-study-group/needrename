#include "Transform.h"

#include <cassert>

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
            m_position +  m_rotation * (m_scale * other.m_position),
            m_rotation * other.m_rotation,
            m_scale * (m_rotation * other.m_scale)
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
        glm::vec3 axis = glm::normalize(axisAngles);
        float angle = axisAngles.x / axis.x;
        assert(abs(axisAngles.y / axis.y - angle) <= 1e-6);
        assert(abs(axisAngles.z / axis.z - angle) <= 1e-6);
        m_rotation = glm::angleAxis(glm::radians(angle), axis);
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

    glm::vec3 Transform::GetRotationEuler() const
    {
        return glm::eulerAngles(m_rotation);
    }

    const glm::quat &Transform::GetRotation() const
    {
        return m_rotation;
    }

    glm::vec3 Transform::GetRotationAxisAngles() const
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
