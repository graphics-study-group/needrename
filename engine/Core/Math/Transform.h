#ifndef CORE_MATH_TRANSFORM_INCLUDED
#define CORE_MATH_TRANSFORM_INCLUDED

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>

namespace Engine
{
    class Transform
    {
    public:
        /// @brief Get the matrix representing the transform
        /// the matrix is constructed such that
        /// ret = scale * location * rotation
        /// @return transform matrix 
        glm::mat4 GetModelMatrix() const;

        /// @brief Apply transform to another transform
        /// @param other 
        /// @return 
        Transform operator*(const Transform & other) const;
    public:
        glm::vec3 m_position {0.0f};
        glm::quat m_rotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_scale {1.0f};
    };
}

#endif // CORE_MATH_TRANSFORM_INCLUDED
