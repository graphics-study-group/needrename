#include "Transform.h"

namespace Engine
{
    glm::mat4 Transform::GetModelMatrix() const
    {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, m_position);
        model = glm::rotate(model, glm::radians(m_rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(m_rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(m_rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, m_scale);
        return model;
    }

    Transform Transform::operator*(const Transform &other) const
    {
        Transform result{
            m_position + other.m_position,
            m_rotation + other.m_rotation,
            m_scale * other.m_scale
        };
        return result;
    }

} // namespace Engine

