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
#include <unordered_set>
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

    void PhysicsAdaptor::SubmitFixedJoint(uint32_t joint_idx, const FixedJointSubmitData &data) {
        m_pending_fixed_joints[joint_idx] = data;
    }

    void PhysicsAdaptor::SubmitHingeJoint(uint32_t joint_idx, const HingeJointSubmitData &data) {
        m_pending_hinge_joints[joint_idx] = data;
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
        if (m_pending_rigid_bodies.empty() && m_pending_shapes.empty() && m_pending_fixed_joints.empty()
            && m_pending_hinge_joints.empty()) {
            return;
        }

        bool has_pending = !m_pending_rigid_bodies.empty() || !m_pending_shapes.empty()
                           || !m_pending_fixed_joints.empty() || !m_pending_hinge_joints.empty();
        if (!has_pending) {
            return;
        }

        // Step 0: Update collision filter declarations from pending shapes
        for (auto &[shape_idx, shape_desc] : m_pending_shapes) {
            ComponentHandle src_handle;
            for (auto &[ch, idx] : m_shape_component_to_index) {
                if (idx == shape_idx) {
                    src_handle = ch;
                    break;
                }
            }
            if (!src_handle.IsValid()) continue;

            m_filter_map[src_handle] = {};
            for (ComponentHandle tgt_handle : shape_desc.ignore_collision_shapes) {
                if (!tgt_handle.IsValid()) continue;
                if (tgt_handle == src_handle) continue;
                m_filter_map[src_handle].insert(tgt_handle);
            }
        }

        // Step 1: COM + inertia computation
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

            m_physics_scene.SubmitCollisionShape(shape_idx, com_desc);
        }

        // Step 3: Collision filter resolution
        uint32_t shape_count = static_cast<uint32_t>(m_shape_component_to_index.size());
        if (shape_count > 0) {
            std::unordered_set<uint64_t> pair_set;
            for (auto &[src_ch, target_set] : m_filter_map) {
                auto src_it = m_shape_component_to_index.find(src_ch);
                if (src_it == m_shape_component_to_index.end()) continue;
                uint32_t src_idx = src_it->second;

                for (ComponentHandle tgt_ch : target_set) {
                    auto tgt_it = m_shape_component_to_index.find(tgt_ch);
                    if (tgt_it == m_shape_component_to_index.end()) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "PhysicsAdaptor: filter target ComponentHandle %u not registered",
                            tgt_ch.GetID()
                        );
                        continue;
                    }
                    uint32_t tgt_idx = tgt_it->second;

                    if (src_idx == tgt_idx) continue;

                    uint32_t a = src_idx < tgt_idx ? src_idx : tgt_idx;
                    uint32_t b = src_idx > tgt_idx ? src_idx : tgt_idx;
                    uint64_t pair_key = (static_cast<uint64_t>(a) << 32) | b;
                    pair_set.insert(pair_key);
                }
            }

            std::vector<uint64_t> pairs(pair_set.begin(), pair_set.end());
            std::sort(pairs.begin(), pairs.end());

            std::vector<std::vector<uint32_t>> per_shape(shape_count);
            uint32_t skipped = 0;

            for (uint64_t pk : pairs) {
                uint32_t a = static_cast<uint32_t>(pk >> 32);
                uint32_t b = static_cast<uint32_t>(pk & 0xFFFFFFFFu);

                if (per_shape[a].size() >= PhysicsScene::MAX_FILTER_ENTRIES
                    || per_shape[b].size() >= PhysicsScene::MAX_FILTER_ENTRIES) {
                    ++skipped;
                    continue;
                }

                per_shape[a].push_back(b);
                per_shape[b].push_back(a);
            }

            if (skipped > 0) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "PhysicsAdaptor: %u filter pairs skipped due to MAX_FILTER_ENTRIES=%u capacity",
                    skipped,
                    PhysicsScene::MAX_FILTER_ENTRIES
                );
            }

            std::vector<uint32_t> filter_data(
                shape_count * PhysicsScene::MAX_FILTER_ENTRIES, PhysicsScene::INVALID_INDEX
            );

            for (uint32_t i = 0; i < shape_count; ++i) {
                std::sort(per_shape[i].begin(), per_shape[i].end());
                auto &list = per_shape[i];
                for (uint32_t k = 0; k < list.size() && k < PhysicsScene::MAX_FILTER_ENTRIES; ++k) {
                    filter_data[i * PhysicsScene::MAX_FILTER_ENTRIES + k] = list[k];
                }
            }

            m_physics_scene.SetShapeFilters(filter_data, shape_count);
        }

        // Step 4: Fixed joint conversion
        for (auto &[joint_idx, data] : m_pending_fixed_joints) {
            auto c1_it = m_com_offsets.find(data.obj1_index);
            auto c2_it = m_com_offsets.find(data.obj2_index);
            glm::vec3 c1 = (c1_it != m_com_offsets.end()) ? c1_it->second : glm::vec3(0.0f);
            glm::vec3 c2 = (c2_it != m_com_offsets.end()) ? c2_it->second : glm::vec3(0.0f);
            GpuFixedJoint joint = detail::JointConverter::ConvertFixed(data, c1, c2);
            m_physics_scene.SubmitFixedJoint(joint_idx, joint);
        }

        // Step 5: Hinge joint conversion
        for (auto &[joint_idx, data] : m_pending_hinge_joints) {
            auto c1_it = m_com_offsets.find(data.obj1_index);
            auto c2_it = m_com_offsets.find(data.obj2_index);
            glm::vec3 c1 = (c1_it != m_com_offsets.end()) ? c1_it->second : glm::vec3(0.0f);
            glm::vec3 c2 = (c2_it != m_com_offsets.end()) ? c2_it->second : glm::vec3(0.0f);
            GpuHingeJoint joint = detail::JointConverter::ConvertHinge(data, c1, c2);
            m_physics_scene.SubmitHingeJoint(joint_idx, joint);
        }

        // Step 6: Clear pending
        m_pending_rigid_bodies.clear();
        m_pending_shapes.clear();
        m_pending_fixed_joints.clear();
        m_pending_hinge_joints.clear();

        // Step 7: GPU sync
        m_physics_scene.SyncGpuBuffers(render_system);
    }
} // namespace Engine
