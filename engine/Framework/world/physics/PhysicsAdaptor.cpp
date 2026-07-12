#include "PhysicsAdaptor.h"

#include "Internal/ComInertiaComputer.hpp"
#include "Internal/JointConverter.hpp"

#include <Framework/component/physics/CollisionShapeComponent.h>
#include <Framework/component/physics/RigidBodyComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Render/RenderSystem.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

namespace Engine {
    PhysicsAdaptor::PhysicsAdaptor(PhysicsScene &physics_scene, Scene &scene) :
        m_physics_scene(physics_scene), m_scene(scene) {
    }

    PhysicsAdaptor::~PhysicsAdaptor() {
    }

    uint32_t PhysicsAdaptor::AllocateSlot(ObjectHandle owner) {
        auto it = m_object_to_rigid_body.find(owner);
        if (it != m_object_to_rigid_body.end()) {
            return it->second;
        }
        uint32_t idx = m_physics_scene.AllocateRigidBodySlot();
        m_object_to_rigid_body[owner] = idx;
        m_rigid_body_to_object[idx] = owner;
        m_rigid_body_to_shapes[idx] = {};
        return idx;
    }

    uint32_t PhysicsAdaptor::AllocateShapeSlot(ComponentHandle owner) {
        auto it = m_shape_component_to_index.find(owner);
        if (it != m_shape_component_to_index.end()) {
            return it->second;
        }
        uint32_t idx = m_physics_scene.AllocateCollisionShapeSlot();
        m_shape_component_to_index[owner] = idx;
        return idx;
    }

    uint32_t PhysicsAdaptor::AllocateFixedJoint() {
        return m_physics_scene.AllocateFixedJoint();
    }

    uint32_t PhysicsAdaptor::AllocateHingeJoint() {
        return m_physics_scene.AllocateHingeJoint();
    }

    void PhysicsAdaptor::SubmitRigidBody(uint32_t index, const RigidBodyDescriptor &desc) {
        m_pending_rigid_bodies[index] = desc;
    }

    void PhysicsAdaptor::SubmitShape(uint32_t index, const CollisionShapeDescriptor &desc) {
        m_pending_shapes[index] = desc;
    }

    void PhysicsAdaptor::SubmitJoint(uint32_t joint_idx, const JointSubmitData &data) {
        m_pending_joints[joint_idx] = data;
    }

    void PhysicsAdaptor::BindShapeToRigidBody(uint32_t shape_idx, uint32_t rb_idx) {
        if (shape_idx == PhysicsScene::INVALID_INDEX) return;

        auto rb_it = m_shape_to_rigid_body.find(shape_idx);
        if (rb_it != m_shape_to_rigid_body.end()) {
            uint32_t old_rb = rb_it->second;
            if (old_rb != PhysicsScene::INVALID_INDEX) {
                auto &list = m_rigid_body_to_shapes[old_rb];
                list.erase(std::remove(list.begin(), list.end(), shape_idx), list.end());
            }
        }

        if (rb_idx == PhysicsScene::INVALID_INDEX) {
            m_shape_to_rigid_body[shape_idx] = PhysicsScene::INVALID_INDEX;
        } else {
            m_shape_to_rigid_body[shape_idx] = rb_idx;
            auto &list = m_rigid_body_to_shapes[rb_idx];
            if (std::find(list.begin(), list.end(), shape_idx) == list.end()) {
                list.push_back(shape_idx);
            }
        }
    }

    uint32_t PhysicsAdaptor::FindRigidBodyByObjectHandle(ObjectHandle handle) const {
        auto it = m_object_to_rigid_body.find(handle);
        if (it == m_object_to_rigid_body.end()) return PhysicsScene::INVALID_INDEX;
        return it->second;
    }

    glm::vec3 PhysicsAdaptor::GetComOffsetLocal(uint32_t rb_idx) const {
        auto it = m_com_offsets.find(rb_idx);
        if (it == m_com_offsets.end()) return glm::vec3(0.0f);
        return it->second;
    }

    bool PhysicsAdaptor::IsPhysicsActive() const {
        return m_physics_active;
    }

    void PhysicsAdaptor::SetPhysicsActive(bool active) {
        m_physics_active = active;
    }

    PhysicsScene &PhysicsAdaptor::GetPhysicsScene() {
        return m_physics_scene;
    }

    void PhysicsAdaptor::Flush(RenderSystem &render_system) {
        if (m_pending_rigid_bodies.empty() && m_pending_shapes.empty() && m_pending_joints.empty()
            && m_resolved_filters.empty()) {
            return;
        }

        bool has_pending = !m_pending_rigid_bodies.empty() || !m_pending_shapes.empty() || !m_pending_joints.empty();
        if (!has_pending) {
            return;
        }

        // Step 1: Resolve collision filters per pending shape
        for (auto &[shape_idx, shape_desc] : m_pending_shapes) {
            if (shape_desc.ignore_collision_objects.empty()) continue;

            std::vector<uint32_t> resolved;
            for (ObjectHandle obj_handle : shape_desc.ignore_collision_objects) {
                GameObject *target_go = m_scene.GetGameObject(obj_handle);
                if (!target_go) continue;

                for (ComponentHandle comp_handle : target_go->m_components) {
                    auto *shape_comp = m_scene.GetComponent<CollisionShapeComponent>(comp_handle);
                    if (!shape_comp) continue;
                    uint32_t target_shape = shape_comp->GetPhysicsShapeIndex();
                    if (target_shape != PhysicsScene::INVALID_INDEX) {
                        resolved.push_back(target_shape);
                    }
                }
            }
            std::sort(resolved.begin(), resolved.end());
            resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());
            m_resolved_filters[shape_idx] = std::move(resolved);
        }

        // Step 2: COM + inertia computation
        std::unordered_map<uint32_t, detail::ShapePose> shape_poses;
        for (auto &[rb_idx, rb_desc] : m_pending_rigid_bodies) {
            std::vector<detail::ShapeComputationData> shape_data;
            auto shapes_it = m_rigid_body_to_shapes.find(rb_idx);
            if (shapes_it != m_rigid_body_to_shapes.end()) {
                for (uint32_t shape_idx : shapes_it->second) {
                    auto pending_it = m_pending_shapes.find(shape_idx);
                    if (pending_it == m_pending_shapes.end()) continue;

                    detail::ShapeComputationData sd;
                    sd.shape_index = shape_idx;
                    sd.type = pending_it->second.type;
                    sd.feature = pending_it->second.feature;
                    sd.world_position = pending_it->second.world_position;
                    sd.world_rotation = pending_it->second.world_rotation;
                    shape_data.push_back(sd);
                }
            }

            detail::ComInertiaOutput output = detail::ComInertiaComputer::Compute(rb_desc, shape_data);

            m_com_offsets[rb_idx] = detail::Vec4ToVec3(output.center_offset_local);

            RigidBodyComDescriptor com_desc;
            com_desc.mass = rb_desc.mass;
            com_desc.static_friction = rb_desc.static_friction;
            com_desc.dynamic_friction = rb_desc.dynamic_friction;
            com_desc.restitution = rb_desc.restitution;
            com_desc.is_kinematic = rb_desc.is_kinematic;
            com_desc.center_world_position = output.center_world_position;
            com_desc.center_world_rotation = output.center_world_rotation;
            com_desc.inertia = output.inertia;
            com_desc.inverse_inertia = output.inverse_inertia;
            com_desc.linear_velocity = detail::ToVec4(rb_desc.linear_velocity);
            com_desc.angular_velocity = detail::ToVec4(rb_desc.angular_velocity);
            com_desc.external_force = detail::ToVec4(rb_desc.external_force);
            com_desc.external_torque = detail::ToVec4(rb_desc.external_torque);

            m_physics_scene.SubmitRigidBody(rb_idx, com_desc);

            shape_poses.insert(output.shape_poses.begin(), output.shape_poses.end());
        }

        // Step 3: Shape COM descriptors
        for (auto &[shape_idx, shape_desc] : m_pending_shapes) {
            CollisionShapeComDescriptor com_desc;
            com_desc.type = static_cast<uint32_t>(shape_desc.type);
            com_desc.feature = detail::ToVec4(shape_desc.feature);
            com_desc.world_position = detail::ToVec4(shape_desc.world_position);
            com_desc.world_rotation = detail::ToVec4(shape_desc.world_rotation);

            auto pose_it = shape_poses.find(shape_idx);
            if (pose_it != shape_poses.end()) {
                com_desc.local_position = pose_it->second.position;
                com_desc.local_rotation = pose_it->second.rotation;
            }

            auto bind_it = m_shape_to_rigid_body.find(shape_idx);
            com_desc.bound_rigid_body =
                (bind_it != m_shape_to_rigid_body.end()) ? bind_it->second : PhysicsScene::INVALID_INDEX;

            auto filter_it = m_resolved_filters.find(shape_idx);
            if (filter_it != m_resolved_filters.end()) {
                com_desc.ignore_shape_indices = filter_it->second;
            }

            m_physics_scene.SubmitCollisionShape(shape_idx, com_desc);
        }

        // Step 4: Joint conversion
        for (auto &[joint_idx, data] : m_pending_joints) {
            const uint32_t jidx = joint_idx;
            std::visit(
                [this, jidx](const auto &d) {
                    using T = std::decay_t<decltype(d)>;
                    auto c1_it = m_com_offsets.find(d.obj1_index);
                    auto c2_it = m_com_offsets.find(d.obj2_index);
                    glm::vec3 c1 = (c1_it != m_com_offsets.end()) ? c1_it->second : glm::vec3(0.0f);
                    glm::vec3 c2 = (c2_it != m_com_offsets.end()) ? c2_it->second : glm::vec3(0.0f);
                    if constexpr (std::is_same_v<T, FixedJointSubmitData>) {
                        GpuFixedJoint joint = detail::JointConverter::ConvertFixed(d, c1, c2);
                        m_physics_scene.SubmitFixedJoint(jidx, joint);
                    } else if constexpr (std::is_same_v<T, HingeJointSubmitData>) {
                        GpuHingeJoint joint = detail::JointConverter::ConvertHinge(d, c1, c2);
                        m_physics_scene.SubmitHingeJoint(jidx, joint);
                    }
                },
                data
            );
        }

        // Step 5: Clear pending
        m_pending_rigid_bodies.clear();
        m_pending_shapes.clear();
        m_pending_joints.clear();
        m_resolved_filters.clear();

        // Step 6: GPU sync
        m_physics_scene.SyncGpuBuffers(render_system);
    }
} // namespace Engine
