#ifndef CORE_MATH_TRANSFORM_INCLUDED
#define CORE_MATH_TRANSFORM_INCLUDED

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

namespace Engine
{
    class Transform
    {
    public:
        Transform()
            : m_position(0.0f)
            , m_rotation(0.0f)
            , m_scale(1.0f)
        {
        }

        glm::mat4 GetModelMatrix() const
        {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, m_position);
            model = glm::rotate(model, glm::radians(m_rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(m_rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(m_rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, m_scale);
            return model;
        }

        Transform operator*(const Transform & other) const
        {
            Transform result;
            result.m_position = m_position + other.m_position;
            result.m_rotation = m_rotation + other.m_rotation;
            result.m_scale = m_scale * other.m_scale;
            return result;
        }

    public:
        glm::vec3 m_position;
        glm::vec3 m_rotation;
        glm::vec3 m_scale;
    };
}

#endif // CORE_MATH_TRANSFORM_INCLUDED