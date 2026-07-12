#include "PhysicsConstraintComponent.h"

#include <Framework/component/Component.h>
#include <Framework/component/physics/RigidBodyComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/physics/PhysicsAdaptor.h>
#include <Reflection/Archive.h>

#include <SDL3/SDL.h>

namespace Engine {

    PhysicsConstraintComponent::PhysicsConstraintComponent(const GameObject &parent) : Component(parent) {
    }

    PhysicsConstraintComponent::~PhysicsConstraintComponent() = default;

    void PhysicsConstraintComponent::Awake() {
        Scene *scene = GetScene();
        if (scene == nullptr) {
            return;
        }

        PhysicsScene *physics_scene = scene->GetPhysicsScene();
        if (physics_scene == nullptr) {
            return;
        }

        PhysicsAdaptor &adaptor = scene->GetPhysicsAdaptor();

        m_joint_indices.clear();
        m_joint_indices.reserve(m_joints.size());

        for (const auto &joint_var : m_joints) {
            std::visit(
                [&](const auto &joint_def) {
                    using T = std::decay_t<decltype(joint_def)>;
                    if constexpr (std::is_same_v<T, FixedJointDef>) {
                        uint32_t idx = adaptor.AllocateFixedJoint();
                        m_joint_indices.push_back({idx, false});
                    } else if constexpr (std::is_same_v<T, HingeJointDef>) {
                        uint32_t idx = adaptor.AllocateHingeJoint();
                        m_joint_indices.push_back({idx, true});
                    }
                },
                joint_var
            );
        }
    }

    void PhysicsConstraintComponent::Init() {
        // All joint data is passed through in GO-local space. Conversion to
        // COM-local space is deferred to PhysicsScene::ConvertPendingJointUpdates,
        // which runs after RecalculateRigidBodyState in InitializePendingRigidBodies.
        Scene *scene = GetScene();
        if (scene == nullptr) {
            return;
        }

        PhysicsScene *physics_scene = scene->GetPhysicsScene();
        if (physics_scene == nullptr) {
            return;
        }

        PhysicsAdaptor &adaptor = scene->GetPhysicsAdaptor();

        ObjectHandle own_handle = GetParentGameObject()->GetHandle();
        uint32_t obj1_index = adaptor.FindRigidBodyByObjectHandle(own_handle);
        if (obj1_index == PhysicsScene::INVALID_INDEX) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "[PhysicsConstraintComponent::Init] Owner object (handle %u) has no RigidBodyComponent.",
                own_handle.GetID()
            );
            return;
        }

        const Transform &t1 = GetParentGameObject()->GetWorldTransform();

        for (size_t i = 0; i < m_joints.size(); ++i) {
            if (i >= m_joint_indices.size()) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "[PhysicsConstraintComponent::Init] Joint count mismatch (Awake may have failed)."
                );
                break;
            }

            uint32_t joint_idx = m_joint_indices[i].first;

            std::visit(
                [&](const auto &joint_def) {
                    using T = std::decay_t<decltype(joint_def)>;

                    uint32_t obj2_index = adaptor.FindRigidBodyByObjectHandle(joint_def.m_obj2_handle);
                    if (obj2_index == PhysicsScene::INVALID_INDEX) {
                        SDL_LogError(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "[PhysicsConstraintComponent::Init] Joint obj2 (handle %u) has no RigidBodyComponent.",
                            joint_def.m_obj2_handle.GetID()
                        );
                        return;
                    }

                    if constexpr (std::is_same_v<T, FixedJointDef>) {
                        GameObject *obj2_go = scene->GetGameObject(joint_def.m_obj2_handle);
                        if (obj2_go == nullptr) {
                            SDL_LogError(
                                SDL_LOG_CATEGORY_APPLICATION,
                                "[PhysicsConstraintComponent::Init] Joint obj2 (handle %u) not found in scene.",
                                joint_def.m_obj2_handle.GetID()
                            );
                            return;
                        }

                        const Transform &t2 = obj2_go->GetWorldTransform();
                        glm::vec3 pos1 = t1.GetPosition();
                        glm::vec3 pos2 = t2.GetPosition();
                        glm::quat q1 = t1.GetRotation();
                        glm::quat q2 = t2.GetRotation();
                        glm::quat q1_inv = glm::inverse(q1);

                        glm::vec3 initial_rel_pos_local = q1_inv * (pos2 - pos1);
                        glm::quat initial_rel_rotation = q1_inv * q2;

                        FixedJointSubmitData submit_data{
                            obj1_index, obj2_index, joint_def.m_compliance, initial_rel_pos_local, initial_rel_rotation
                        };
                        adaptor.SubmitJoint(joint_idx, submit_data);
                    } else if constexpr (std::is_same_v<T, HingeJointDef>) {
                        // Resolve obj2 transform for initial relative transform computation.
                        GameObject *obj2_go = scene->GetGameObject(joint_def.m_obj2_handle);
                        if (obj2_go == nullptr) {
                            SDL_LogError(
                                SDL_LOG_CATEGORY_APPLICATION,
                                "[PhysicsConstraintComponent] HingeJoint obj2 (handle %u) not found in scene.",
                                joint_def.m_obj2_handle.GetID()
                            );
                            return;
                        }

                        const Transform &t2 = obj2_go->GetWorldTransform();
                        glm::vec3 pos2 = t2.GetPosition();
                        glm::quat q2 = t2.GetRotation();

                        // Normalize and validate hinge axis.
                        glm::vec3 axis = joint_def.m_hinge_axis_obj1;
                        if (glm::length(axis) > 0.0f) {
                            axis = glm::normalize(axis);
                        }
                        if (glm::length(axis) < 1e-6f) {
                            SDL_LogError(
                                SDL_LOG_CATEGORY_APPLICATION,
                                "[PhysicsConstraintComponent] HingeJoint obj2 (handle %u) has a zero-length hinge "
                                "axis.",
                                joint_def.m_obj2_handle.GetID()
                            );
                            return;
                        }

                        // Compute initial relative transform (pos before rot, same as FixedJoint).
                        glm::vec3 pos1 = t1.GetPosition();
                        glm::quat q1 = t1.GetRotation();
                        glm::quat q1_inv = glm::inverse(q1);
                        glm::vec3 initial_rel_pos_local = q1_inv * (pos2 - pos1);
                        glm::quat initial_rel_rotation = q1_inv * q2;

                        HingeJointSubmitData submit_data{
                            obj1_index,
                            obj2_index,
                            joint_def.m_compliance,
                            axis,
                            joint_def.m_hinge_anchor_obj1,
                            initial_rel_pos_local,
                            initial_rel_rotation
                        };
                        adaptor.SubmitJoint(joint_idx, submit_data);
                    }
                },
                m_joints[i]
            );
        }
    }

    void PhysicsConstraintComponent::save_to_archive(Serialization::Archive &archive) const {
        // Let the base class handle Component::m_handle + generated fields.
        Component::save_to_archive(archive);

        Serialization::Json &json = *archive.m_cursor;
        Serialization::Json joints_array = Serialization::Json::array();

        for (const auto &joint_var : m_joints) {
            Serialization::Json j = Serialization::Json::object();
            std::visit(
                [&j](const auto &def) {
                    using T = std::decay_t<decltype(def)>;
                    if constexpr (std::is_same_v<T, FixedJointDef>) {
                        j["type"] = "fixed";
                        j["obj2_handle"] = def.m_obj2_handle.GetID();
                        j["compliance"] = def.m_compliance;
                    } else if constexpr (std::is_same_v<T, HingeJointDef>) {
                        j["type"] = "hinge";
                        j["obj2_handle"] = def.m_obj2_handle.GetID();
                        j["compliance"] = def.m_compliance;
                        j["hinge_axis"] = {def.m_hinge_axis_obj1.x, def.m_hinge_axis_obj1.y, def.m_hinge_axis_obj1.z};
                        j["hinge_anchor"] = {
                            def.m_hinge_anchor_obj1.x, def.m_hinge_anchor_obj1.y, def.m_hinge_anchor_obj1.z
                        };
                    }
                },
                joint_var
            );
            joints_array.push_back(std::move(j));
        }
        json["m_joints"] = std::move(joints_array);
    }

    void PhysicsConstraintComponent::load_from_archive(Serialization::Archive &archive) {
        // Let the base class restore Component::m_handle + generated fields.
        Component::load_from_archive(archive);

        Serialization::Json &json = *archive.m_cursor;
        if (!json.contains("m_joints") || !json["m_joints"].is_array()) {
            return;
        }

        m_joints.clear();
        for (const auto &j : json["m_joints"]) {
            const std::string type = j.value("type", "");
            if (type == "fixed") {
                FixedJointDef def;
                def.m_obj2_handle = ObjectHandle(j.value("obj2_handle", 0u));
                def.m_compliance = j.value("compliance", 0.0f);
                m_joints.push_back(def);
            } else if (type == "hinge") {
                HingeJointDef def;
                def.m_obj2_handle = ObjectHandle(j.value("obj2_handle", 0u));
                def.m_compliance = j.value("compliance", 0.0f);
                if (j.contains("hinge_axis") && j["hinge_axis"].is_array() && j["hinge_axis"].size() >= 3) {
                    def.m_hinge_axis_obj1 = glm::vec3(
                        j["hinge_axis"][0].get<float>(),
                        j["hinge_axis"][1].get<float>(),
                        j["hinge_axis"][2].get<float>()
                    );
                }
                if (j.contains("hinge_anchor") && j["hinge_anchor"].is_array() && j["hinge_anchor"].size() >= 3) {
                    def.m_hinge_anchor_obj1 = glm::vec3(
                        j["hinge_anchor"][0].get<float>(),
                        j["hinge_anchor"][1].get<float>(),
                        j["hinge_anchor"][2].get<float>()
                    );
                }
                m_joints.push_back(def);
            }
        }
    }

} // namespace Engine

#include "__generated__/PhysicsConstraintComponent.h.inc"
