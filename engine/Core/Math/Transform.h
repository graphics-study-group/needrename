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

        /// @brief Set the position component of transform
        /// @param position 
        /// @return this for chaining set operations
        Transform & SetPosition(glm::vec3 position);

        /// @brief Set the position component of transform with nautical euler angles (Tait–Bryan angles)
        /// , in pitch-roll-yaw order.
        /// @param euler 
        /// @return this for chaining set operations
        Transform & SetRotationEuler(glm::vec3 euler); 

        /// @brief Set the position component of transform with quaternion
        /// @param quat 
        /// @return this for chaining set operations
        Transform & SetRotation(glm::quat quat);

        /// @brief Set the rotation component of transform, using axis-angles representation
        /// @param axisAngles 
        /// @return this for chaining set operations
        Transform & SetRotationAxisAngles(glm::vec3 axisAngles);

        /// @brief Set the scale component of transform
        /// @param scale 
        /// @return this for chaining set operations
        Transform & SetScale(glm::vec3 scale);

        const glm::vec3& GetPosition() const;
        glm::vec3 GetEulerAngles() const;
        const glm::quat& GetQuat() const;
        glm::vec3 GetAxisAngles() const;
        const glm::vec3& GetScale() const;
    public:
        glm::vec3 m_position {0.0f};
        glm::quat m_rotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_scale {1.0f};
    };
}

#endif // CORE_MATH_TRANSFORM_INCLUDED
