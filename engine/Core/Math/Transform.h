#ifndef CORE_MATH_TRANSFORM_INCLUDED
#define CORE_MATH_TRANSFORM_INCLUDED

#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>

namespace Engine {
    /**
     * @brief Base class for geometric transform of objects in the engine.
     *
     * @section transform_coordinate_system Coordinate System
     *
     * Our _world coordinate system_ is in accord with _Blender_.
     * We choose a right-handed cartesian frame, with X pointing right of the
     * screen, Y pointing front (i.e. pointing into the screen) and Z pointing up.
     * Namely, this is a RFU coordinate system.
     * The camera in the world are always set to look at the front direction (Y+).
     *
     * @subsection transform_rotation Rotation
     *
     * Our _rotation_ is in general represented by quaternions, supported by GLM
     * library and stored in w-x-y-z order.
     *
     * For euler angle representations, the convertion is done with the help of
     * `glm::quat(glm::vec3)` constructor, and follows the exactly same convention.
     * As of writing, the angles should be saved in _X-Y-Z_ order in _radians_,
     * and the rotation is applied in such order.
     * For axis-angle representations of rotation, we use degrees as units.
     *
     * @subsection transform_order_of_application Order of Application
     *
     * The order of _transforms_, consist of translation, rotation and scaling,
     * is as follows.
     * First, scaling is applied.
     * Then, the frame is rotated.
     * Finally, translation is applied.
     * This procedure produces model matrix satisfying
     *  \f[
     *  M = T R S
     *  \f]
     *
     * @internal
     *
     * @section transform_details Implementational Details
     *
     * @subsection transform_view_space_transform View Space Transform
     *
     * Note that the screen coordinate system (i.e. normalized device
     * coordinate) for Vulkan is defined as: X pointing to the right of the
     * screen, Y pointing to the top of the screen, and Z is chosen when
     * specifying depth comparison operation.
     * If the operation is chosen as `VK_COMPARE_OP_LESS`, then fragments with
     * lower depth values will be presented, and therefore Z points inwards to
     * the screen.
     * This convention is somewhat different from our world space covention,
     * where Y+ points to front.
     *
     * A corollary follows that if the view space of a camera and the world
     * space is identified, namely, the view matrix is the identity matrix, and
     * the depth comparision operation is chosen to be less or less and equal,
     * then the camera will be looking at the bottom of the world (Z-).
     *
     * However, if you set the view matrix of the camera with
     * `Engine::CameraComponent::UpdateViewMatrix()` method, then the view
     * matrix will be automatically generated as if the camera is facing towards
     * the Y+ direction (i.e. front) in its local frame.
     * For euler angle, this specifically means that while they are in _X-Y-Z_
     * order, their meaning is not _pitch-yaw-roll_ as in the Vulkan NDC space,
     * but _pitch-roll-yaw_ as in the world space.
     * Therefore, for your sanity, you should _avoid_ directly manipulating the
     * view matrix, and always use the abstracted `Transform` interface.
     *
     * @endinternal
     */
    class REFL_SER_CLASS(REFL_BLACKLIST) Transform {
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
        Transform &SetPosition(glm::vec3 position);

        /// @brief Set the position component of transform with nautical euler angles (Tait–Bryan angles)
        /// , in pitch-roll-yaw (X-Y-Z) order.
        /// @param euler angle in radians
        /// @return this for chaining set operations
        Transform &SetRotationEuler(glm::vec3 euler);

        /// @brief Set the position component of transform with quaternion
        /// @param quat
        /// @return this for chaining set operations
        Transform &SetRotation(glm::quat quat);

        /// @brief Set the rotation component of transform, using axis-angles representation
        /// @param axisAngles angle in degrees * normalized rotation axis
        /// @return this for chaining set operations
        Transform &SetRotationAxisAngles(glm::vec3 axisAngles);

        /// @brief Set the scale component of transform
        /// @param scale
        /// @return this for chaining set operations
        Transform &SetScale(glm::vec3 scale);

        /// @brief Get the position component
        /// @return position
        const glm::vec3 &GetPosition() const;

        /// @brief Get the euler angle representation of rotation
        /// @return a copy of euler angle in pitch-roll-yaw order.
        glm::vec3 GetRotationEuler() const;

        /// @brief Get quaternion representation of rotation
        /// @return quaternion
        const glm::quat &GetRotation() const;

        /// @brief Get axis-angle representation of rotation
        /// @return axis-angle representation (angle in degrees * normalized rotation axis)
        glm::vec3 GetRotationAxisAngles() const;

        /// @brief Get scale component of transform
        /// @return scale
        const glm::vec3 &GetScale() const;

    public:
        glm::vec3 m_position{0.0f};
        glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_scale{1.0f};
    };
} // namespace Engine

#endif // CORE_MATH_TRANSFORM_INCLUDED
