#ifndef FRAMEWORK_BRIDGE_INTERNAL_COMINERTIACOMPUTER_INCLUDED
#define FRAMEWORK_BRIDGE_INTERNAL_COMINERTIACOMPUTER_INCLUDED

#include "../PhysicsDescriptors.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/quaternion.hpp>

#include <unordered_map>
#include <vector>

namespace Engine {
    namespace detail {

        /**
         * @brief Per-shape input data for center-of-mass and inertia computation.
         *
         * Carries the shape's world pose and geometry, extracted from the pending
         * GO-space CollisionShapeDescriptor during Flush.
         */
        struct ShapeComputationData {
            uint32_t shape_index{0};
            CollisionShapeType type{CollisionShapeType::Box};
            glm::vec3 feature{0.0f, 0.0f, 0.0f};
            glm::vec3 world_position{0.0f, 0.0f, 0.0f};
            glm::quat world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        };

        /**
         * @brief COM-local pose of a shape after center-of-mass computation.
         */
        struct ShapePose {
            glm::vec4 position;
            glm::vec4 rotation;
        };

        /**
         * @brief Output of center-of-mass and inertia tensor computation.
         *
         * center_offset_local is the vector from GO origin to COM in GO-local space.
         * shape_poses maps each shape index to its COM-local pose.
         */
        struct ComInertiaOutput {
            glm::vec4 center_world_position{0.0f};
            glm::vec4 center_world_rotation{0.0f};
            glm::vec4 center_offset_local{0.0f};
            glm::mat4 inertia{0.0f};
            glm::mat4 inverse_inertia{0.0f};
            std::unordered_map<uint32_t, ShapePose> shape_poses{};
        };

        inline glm::vec4 ToVec4(const glm::vec3 &v) {
            return glm::vec4(v.x, v.y, v.z, 0.0f);
        }

        inline glm::vec4 ToVec4(const glm::quat &q) {
            return glm::vec4(q.x, q.y, q.z, q.w);
        }

        inline glm::vec3 Vec4ToVec3(const glm::vec4 &v) {
            return glm::vec3(v.x, v.y, v.z);
        }

        inline glm::quat Vec4ToQuat(const glm::vec4 &v) {
            return glm::quat(v.w, v.x, v.y, v.z);
        }

        inline float ComputeBoxVolume(const glm::vec3 &feature) {
            return 8.0f * feature.x * feature.y * feature.z;
        }

        inline float ComputeSphereVolume(float radius) {
            return (4.0f / 3.0f) * glm::pi<float>() * radius * radius * radius;
        }

        inline float ComputeCylinderVolume(float radius, float half_height) {
            return glm::pi<float>() * radius * radius * 2.0f * half_height;
        }

        inline glm::mat3 ComputeBoxInertia(float mass, const glm::vec3 &feature) {
            const float hx = feature.x;
            const float hy = feature.y;
            const float hz = feature.z;
            glm::mat3 inertia(0.0f);
            inertia[0][0] = (mass / 3.0f) * (hy * hy + hz * hz);
            inertia[1][1] = (mass / 3.0f) * (hx * hx + hz * hz);
            inertia[2][2] = (mass / 3.0f) * (hx * hx + hy * hy);
            return inertia;
        }

        inline glm::mat3 ComputeSphereInertia(float mass, float radius) {
            const float diag = (2.0f / 5.0f) * mass * radius * radius;
            glm::mat3 inertia(0.0f);
            inertia[0][0] = diag;
            inertia[1][1] = diag;
            inertia[2][2] = diag;
            return inertia;
        }

        inline glm::mat3 ComputeCylinderInertia(float mass, float radius, float half_height) {
            const float h = 2.0f * half_height;
            const float r2 = radius * radius;
            const float h2 = h * h;
            glm::mat3 inertia(0.0f);
            inertia[0][0] = (mass / 12.0f) * (3.0f * r2 + h2);
            inertia[1][1] = (mass / 12.0f) * (3.0f * r2 + h2);
            inertia[2][2] = (mass / 2.0f) * r2;
            return inertia;
        }

        /**
         * @brief Pure-function module that computes the center of mass and inertia tensor
         *        for a rigid body from its attached collision shapes.
         *
         * Supports volume-weighted automatic COM, manual inertia/COM override,
         * parallel-axis theorem for inertia tensor accumulation, and shape COM-local
         * pose recomputation.
         */
        class ComInertiaComputer {
        public:
            /**
             * @brief Compute center of mass, inertia, and shape local poses.
             *
             * If the rigid body has manual inertia enabled, the manual values are used
             * directly. Otherwise, COM is computed by volume-weighted average of shape
             * world positions, and the inertia tensor is accumulated using the parallel
             * axis theorem.
             *
             * @param rb_desc The GO-space rigid body descriptor containing mass, manual inertia override, and GO-world pose.
             * @param shapes  The list of collision shapes attached to this rigid body, with GO-world poses.
             * @return Computed COM position/rotation, GO→COM offset, inertia tensor, inverse inertia, and per-shape COM-local poses.
             */
            static ComInertiaOutput Compute(
                const RigidBodyDescriptor &rb_desc, const std::vector<ShapeComputationData> &shapes
            ) {
                ComInertiaOutput output;

                const glm::vec3 object_world_position = rb_desc.world_position;
                const glm::quat object_world_rotation = rb_desc.world_rotation;

                if (shapes.empty()) {
                    output.center_world_position = ToVec4(object_world_position);
                    output.center_world_rotation = ToVec4(object_world_rotation);
                    output.center_offset_local = glm::vec4(0.0f);
                    output.inertia = glm::mat4(0.0f);
                    output.inverse_inertia = glm::mat4(0.0f);
                    return output;
                }

                if (rb_desc.use_manual_inertia_com) {
                    output.inertia = glm::mat4(rb_desc.manual_inertia);
                    const float det = glm::determinant(rb_desc.manual_inertia);
                    if (det > 1e-12f) {
                        output.inverse_inertia = glm::mat4(glm::inverse(rb_desc.manual_inertia));
                    } else {
                        output.inverse_inertia = glm::mat4(0.0f);
                    }
                    const glm::vec3 manual_com = rb_desc.manual_center_of_mass;
                    const glm::vec3 center_world_pos = object_world_position + object_world_rotation * manual_com;
                    output.center_world_position = ToVec4(center_world_pos);
                    output.center_world_rotation = ToVec4(object_world_rotation);
                    output.center_offset_local = ToVec4(manual_com);

                    const glm::quat inv_center_rot = glm::inverse(object_world_rotation);
                    for (const auto &shape : shapes) {
                        const glm::vec3 swp = shape.world_position;
                        const glm::quat swr = glm::normalize(shape.world_rotation);
                        ShapePose pose;
                        pose.position = ToVec4(inv_center_rot * (swp - center_world_pos));
                        pose.rotation = ToVec4(glm::normalize(inv_center_rot * swr));
                        output.shape_poses[shape.shape_index] = pose;
                    }
                    return output;
                }

                float total_volume = 0.0f;
                glm::vec3 weighted_center_world(0.0f, 0.0f, 0.0f);

                for (const auto &shape : shapes) {
                    const glm::vec3 feature = glm::abs(shape.feature);
                    float volume = 0.0f;

                    switch (shape.type) {
                    case CollisionShapeType::Box:
                        volume = ComputeBoxVolume(feature);
                        break;
                    case CollisionShapeType::Sphere:
                        volume = ComputeSphereVolume(feature.x);
                        break;
                    case CollisionShapeType::Cylinder:
                        volume = ComputeCylinderVolume(feature.x, feature.y);
                        break;
                    }

                    if (volume > 0.0f) {
                        total_volume += volume;
                        weighted_center_world += shape.world_position * volume;
                    }
                }

                if (total_volume <= 0.0f) {
                    output.center_world_position = ToVec4(object_world_position);
                    output.center_world_rotation = ToVec4(object_world_rotation);
                    output.center_offset_local = glm::vec4(0.0f);
                    output.inertia = glm::mat4(0.0f);
                    output.inverse_inertia = glm::mat4(0.0f);
                    return output;
                }

                glm::vec3 center_world_position = weighted_center_world / total_volume;
                const glm::quat center_world_rotation = object_world_rotation;
                const glm::quat inv_center_world_rotation = glm::inverse(center_world_rotation);
                const glm::vec3 center_offset_local_position =
                    inv_center_world_rotation * (center_world_position - object_world_position);

                output.center_world_position = ToVec4(center_world_position);
                output.center_world_rotation = ToVec4(center_world_rotation);
                output.center_offset_local = ToVec4(center_offset_local_position);

                glm::mat3 inertia_tensor(0.0f);
                const float total_mass = std::max(rb_desc.mass, 0.0f);
                const float fallback_mass = shapes.empty() ? 0.0f : (total_mass / static_cast<float>(shapes.size()));

                for (const auto &shape : shapes) {
                    const glm::vec3 feature = glm::abs(shape.feature);

                    float volume = 0.0f;
                    switch (shape.type) {
                    case CollisionShapeType::Box:
                        volume = ComputeBoxVolume(feature);
                        break;
                    case CollisionShapeType::Sphere:
                        volume = ComputeSphereVolume(feature.x);
                        break;
                    case CollisionShapeType::Cylinder:
                        volume = ComputeCylinderVolume(feature.x, feature.y);
                        break;
                    }

                    if (volume <= 0.0f) {
                        continue;
                    }

                    const float mass = total_volume > 1e-6f ? (total_mass * (volume / total_volume)) : fallback_mass;

                    const glm::vec3 shape_world_position = shape.world_position;
                    const glm::quat shape_world_rotation = glm::normalize(shape.world_rotation);

                    const glm::vec3 shape_local_position =
                        inv_center_world_rotation * (shape_world_position - center_world_position);
                    const glm::quat shape_local_rotation =
                        glm::normalize(inv_center_world_rotation * shape_world_rotation);

                    ShapePose pose;
                    pose.position = ToVec4(shape_local_position);
                    pose.rotation = ToVec4(shape_local_rotation);
                    output.shape_poses[shape.shape_index] = pose;

                    glm::mat3 inertia_shape(0.0f);
                    switch (shape.type) {
                    case CollisionShapeType::Box:
                        inertia_shape = ComputeBoxInertia(mass, feature);
                        break;
                    case CollisionShapeType::Sphere:
                        inertia_shape = ComputeSphereInertia(mass, feature.x);
                        break;
                    case CollisionShapeType::Cylinder:
                        inertia_shape = ComputeCylinderInertia(mass, feature.x, feature.y);
                        break;
                    }

                    const glm::mat3 rotation_matrix = glm::mat3_cast(shape_local_rotation);
                    const glm::mat3 rotated_inertia = rotation_matrix * inertia_shape * glm::transpose(rotation_matrix);

                    const glm::vec3 d = shape_local_position;
                    const float d2 = glm::dot(d, d);
                    const glm::mat3 identity(1.0f);
                    const glm::mat3 parallel_axis = mass * ((d2 * identity) - glm::outerProduct(d, d));

                    inertia_tensor += rotated_inertia + parallel_axis;
                }

                output.inertia = glm::mat4(inertia_tensor);
                const float det = glm::determinant(inertia_tensor);
                if (glm::abs(det) > 1e-12f) {
                    output.inverse_inertia = glm::mat4(glm::inverse(inertia_tensor));
                } else {
                    output.inverse_inertia = glm::mat4(0.0f);
                }

                return output;
            }
        };

    } // namespace detail
} // namespace Engine

#endif // FRAMEWORK_BRIDGE_INTERNAL_COMINERTIACOMPUTER_INCLUDED
