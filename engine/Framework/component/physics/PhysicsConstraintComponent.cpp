#include "PhysicsConstraintComponent.h"

#include <Framework/component/Component.h>
#include <Framework/component/physics/RigidBodyComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Physics/PhysicsScene.h>

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

} // namespace Engine

#include "__generated__/PhysicsConstraintComponent.h.inc"
