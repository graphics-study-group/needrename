#ifndef CORE_MATH_TRANSFORM_INCLUDED
#define CORE_MATH_TRANSFORM_INCLUDED

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>
#include <Reflection/macros.h>

namespace Engine
{
    class REFL_SER_CLASS(REFL_BLACKLIST) Transform
    {
        REFL_SER_BODY(Transform)
    public:
        Transform() = default;
        Transform(glm::vec3 position, glm::quat rotation, glm::vec3 scale);
        virtual ~Transform() = default;
        
        /// @brief Get the matrix representing the transform
        /// the matrix is constructed such that
        /// ret = scale * location * rotation
        /// @return transform matrix 
        glm::mat4 GetTransformMatrix() const;

        /// @brief Decomposite a transform matrix into translation, rotation and scale components.
        /// Causes error if the matrix contains other components (i.e. skew or perspective).
        /// @param mat
        void Decompose(glm::mat4 mat);

        [[deprecated]]
        REFL_DISABLE Transform operator*(const Transform &other) const;

        /// @brief Set the position component of transform
        /// @param position 
        /// @return this for chaining set operations
        Transform & SetPosition(glm::vec3 position);

        /// @brief Set the position component of transform with nautical euler angles (Tait–Bryan angles)
        /// , in pitch-roll-yaw order.
        /// @param euler angle in radians
        /// @return this for chaining set operations
        Transform & SetRotationEuler(glm::vec3 euler); 

        /// @brief Set the position component of transform with quaternion
        /// @param quat 
        /// @return this for chaining set operations
        Transform & SetRotation(glm::quat quat);

        /// @brief Set the rotation component of transform, using axis-angles representation
        /// @param axisAngles angle in degrees * normalized rotation axis
        /// @return this for chaining set operations
        Transform & SetRotationAxisAngles(glm::vec3 axisAngles);

        /// @brief Set the scale component of transform
        /// @param scale 
        /// @return this for chaining set operations
        Transform & SetScale(glm::vec3 scale);

        /// @brief Get the position component
        /// @return position
        const glm::vec3& GetPosition() const;

        /// @brief Get the euler angle representation of rotation
        /// @return a copy of euler angle in pitch-roll-yaw order.
        glm::vec3 GetRotationEuler() const;

        /// @brief Get quaternion representation of rotation
        /// @return quaternion
        const glm::quat& GetRotation() const;

        /// @brief Get axis-angle representation of rotation
        /// @return axis-angle representation (angle in degrees * normalized rotation axis)
        glm::vec3 GetRotationAxisAngles() const;

        /// @brief Get scale component of transform
        /// @return scale
        const glm::vec3& GetScale() const;

    public:
        glm::vec3 m_position {0.0f};
        glm::quat m_rotation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_scale {1.0f};
    };
}

#endif // CORE_MATH_TRANSFORM_INCLUDED
