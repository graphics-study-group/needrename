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

} // namespace Engine

