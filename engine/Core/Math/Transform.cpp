#include "Transform.h"

#include <cassert>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/matrix_decompose.hpp>

namespace Engine {
    Transform::Transform(glm::vec3 position, glm::quat rotation, glm::vec3 scale) :
        m_position(position), m_rotation(rotation), m_scale(scale) {
    }

    glm::mat4 Transform::GetTransformMatrix() const {
        glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), m_position);
        modelMatrix = modelMatrix * glm::mat4_cast(m_rotation);
        modelMatrix = glm::scale(modelMatrix, m_scale);
        return modelMatrix;
    }

    void Transform::Decompose(glm::mat4 mat) {
        glm::vec3 skew;
        glm::vec4 persp;
        bool success = glm::decompose(mat, m_scale, m_rotation, m_position, skew, persp);
        assert(success && "Failed to decompose matrix, perspective matrix is singular.");
        assert(glm::length(skew) <= 1e-3 && "Matrix contains skew component.");
        assert(glm::distance(persp, glm::vec4{0.0, 0.0, 0.0, 1.0}) <= 1e-3 && "Matrix contains perspective component.");
    }

    Transform Transform::operator*(const Transform &other) const {
        Transform result{
            m_position + m_rotation * (m_scale * other.m_position),
            m_rotation * other.m_rotation,
            m_scale * (m_rotation * other.m_scale)
        };
        return result;
    }

    Transform &Transform::SetPosition(glm::vec3 position) {
        m_position = position;
        return *this;
    }

    Transform &Transform::SetRotationEuler(glm::vec3 euler) {
        m_rotation = glm::quat(euler);
        assert(glm::abs(glm::length(m_rotation) - 1) <= 1e-6);
        return *this;
    }

    Transform &Transform::SetRotation(glm::quat quat) {
        m_rotation = glm::normalize(quat);
        assert(glm::abs(glm::length(m_rotation) - 1) <= 1e-6);
        return *this;
    }

    Transform &Transform::SetRotationAxisAngles(glm::vec3 axisAngles) {
        glm::vec3 axis = glm::normalize(axisAngles);
        assert(abs(axis.x) > 1e-3);
        float angle = axisAngles.x / axis.x;
        assert(abs(axis.y) <= 1e-6 || abs(axisAngles.y / axis.y - angle) <= 1e-6);
        assert(abs(axis.z) <= 1e-6 || abs(axisAngles.z / axis.z - angle) <= 1e-6);
        m_rotation = glm::angleAxis(glm::radians(angle), axis);
        assert(glm::abs(glm::length(m_rotation) - 1) <= 1e-6);
        return *this;
    }

    Transform &Transform::SetScale(glm::vec3 scale) {
        m_scale = scale;
        return *this;
    }

    const glm::vec3 &Transform::GetPosition() const {
        return m_position;
    }

    glm::vec3 Transform::GetRotationEuler() const {
        return glm::eulerAngles(m_rotation);
    }

    const glm::quat &Transform::GetRotation() const {
        return m_rotation;
    }

    glm::vec3 Transform::GetRotationAxisAngles() const {
        glm::vec3 axis = glm::axis(m_rotation);
        float angle = glm::degrees(glm::angle(m_rotation));
        return axis * angle;
    }

    const glm::vec3 &Transform::GetScale() const {
        return m_scale;
    }
} // namespace Engine
