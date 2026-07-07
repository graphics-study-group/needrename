#include "PhysicsConstraintComponent.h"

#include <Framework/component/Component.h>
#include <Framework/component/physics/RigidBodyComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Physics/PhysicsScene.h>
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

        // Find own rigid body index via the parent GameObject handle.
        ObjectHandle own_handle = GetParentGameObject()->GetHandle();
        uint32_t obj1_index = physics_scene->FindRigidBodyByObjectHandle(own_handle);
        if (obj1_index == PhysicsScene::INVALID_INDEX) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "[PhysicsConstraintComponent] Owner object (handle %u) has no RigidBodyComponent.",
                own_handle.GetID()
            );
            return;
        }

        // Get owner's transform for FixedJoint initial relative transform computation.
        const Transform &t1 = GetParentGameObject()->GetWorldTransform();

        // Process each joint definition.
        for (const auto &joint_var : m_joints) {
            std::visit(
                [&](const auto &joint_def) {
                    using T = std::decay_t<decltype(joint_def)>;

                    // Find obj2 rigid body index via handle.
                    uint32_t obj2_index = physics_scene->FindRigidBodyByObjectHandle(joint_def.m_obj2_handle);
                    if (obj2_index == PhysicsScene::INVALID_INDEX) {
                        SDL_LogError(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "[PhysicsConstraintComponent] Joint obj2 (handle %u) has no RigidBodyComponent.",
                            joint_def.m_obj2_handle.GetID()
                        );
                        return;
                    }

                    if constexpr (std::is_same_v<T, FixedJointDef>) {
                        // Get obj2 transform for initial relative transform calculation.
                        GameObject *obj2_go = scene->GetGameObject(joint_def.m_obj2_handle);
                        if (obj2_go == nullptr) {
                            SDL_LogError(
                                SDL_LOG_CATEGORY_APPLICATION,
                                "[PhysicsConstraintComponent] Joint obj2 (handle %u) not found in scene.",
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

                        physics_scene->RegisterFixedJoint(
                            obj1_index, obj2_index, joint_def.m_compliance, initial_rel_pos_local, initial_rel_rotation
                        );
                    } else if constexpr (std::is_same_v<T, HingeJointDef>) {
                        physics_scene->RegisterHingeJoint(
                            obj1_index,
                            obj2_index,
                            joint_def.m_compliance,
                            joint_def.m_obj1_local_aligned_axis,
                            joint_def.m_obj2_local_aligned_axis,
                            joint_def.m_obj1_local_attach_point,
                            joint_def.m_obj2_local_attach_point
                        );
                    }
                },
                joint_var
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
                        j["obj1_axis"] = {
                            def.m_obj1_local_aligned_axis.x,
                            def.m_obj1_local_aligned_axis.y,
                            def.m_obj1_local_aligned_axis.z
                        };
                        j["obj2_axis"] = {
                            def.m_obj2_local_aligned_axis.x,
                            def.m_obj2_local_aligned_axis.y,
                            def.m_obj2_local_aligned_axis.z
                        };
                        j["obj1_attach"] = {
                            def.m_obj1_local_attach_point.x,
                            def.m_obj1_local_attach_point.y,
                            def.m_obj1_local_attach_point.z
                        };
                        j["obj2_attach"] = {
                            def.m_obj2_local_attach_point.x,
                            def.m_obj2_local_attach_point.y,
                            def.m_obj2_local_attach_point.z
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
                if (j.contains("obj1_axis") && j["obj1_axis"].is_array() && j["obj1_axis"].size() >= 3) {
                    def.m_obj1_local_aligned_axis = glm::vec3(
                        j["obj1_axis"][0].get<float>(), j["obj1_axis"][1].get<float>(), j["obj1_axis"][2].get<float>()
                    );
                }
                if (j.contains("obj2_axis") && j["obj2_axis"].is_array() && j["obj2_axis"].size() >= 3) {
                    def.m_obj2_local_aligned_axis = glm::vec3(
                        j["obj2_axis"][0].get<float>(), j["obj2_axis"][1].get<float>(), j["obj2_axis"][2].get<float>()
                    );
                }
                if (j.contains("obj1_attach") && j["obj1_attach"].is_array() && j["obj1_attach"].size() >= 3) {
                    def.m_obj1_local_attach_point = glm::vec3(
                        j["obj1_attach"][0].get<float>(),
                        j["obj1_attach"][1].get<float>(),
                        j["obj1_attach"][2].get<float>()
                    );
                }
                if (j.contains("obj2_attach") && j["obj2_attach"].is_array() && j["obj2_attach"].size() >= 3) {
                    def.m_obj2_local_attach_point = glm::vec3(
                        j["obj2_attach"][0].get<float>(),
                        j["obj2_attach"][1].get<float>(),
                        j["obj2_attach"][2].get<float>()
                    );
                }
                m_joints.push_back(def);
            }
        }
    }

} // namespace Engine

#include "__generated__/PhysicsConstraintComponent.h.inc"
