#include "CollisionShapeComponent.h"

#include "RigidBodyComponent.h"

#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>

#include <SDL3/SDL.h>

namespace Engine {
    CollisionShapeComponent::CollisionShapeComponent(const GameObject &parent) : Component(parent) {
    }

    CollisionShapeComponent::~CollisionShapeComponent() {
        auto *scene = GetScene();
        if (scene == nullptr || m_shape_index == PhysicsScene::INVALID_INDEX) {
            return;
        }

        if (auto *physics_scene = scene->GetPhysicsScene()) {
            physics_scene->UnregisterCollisionShape(m_shape_index);
        }
    }

    void CollisionShapeComponent::Awake() {
        auto *scene = GetScene();
        auto *owner = GetParentGameObject();
        if (scene == nullptr || owner == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CollisionShapeComponent awake failed: missing scene or owner");
            return;
        }

        auto *physics_scene = scene->GetPhysicsScene();
        if (physics_scene == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CollisionShapeComponent awake failed: physics scene missing");
            return;
        }

        const uint32_t existing_index = physics_scene->FindShapeByComponentHandle(GetHandle());
        if (existing_index != PhysicsScene::INVALID_INDEX) {
            m_shape_index = existing_index;
            return;
        }

        CollisionBoxFeature box_feature{};
        box_feature.m_half_extents = m_box_size * 0.5f;
        box_feature.m_center = m_box_center;
        box_feature.m_rotation = m_box_rotation;

        Transform world_transform = owner->GetWorldTransform();
        glm::vec3 world_center = world_transform.GetPosition() + world_transform.GetRotation() * m_box_center;
        glm::quat world_rotation = glm::normalize(world_transform.GetRotation() * m_box_rotation);

        m_shape_index =
            physics_scene->RegisterCollisionShape(GetHandle(), m_shape_type, box_feature, world_center, world_rotation);

        TryAttachToAncestorRigidBody();
    }

    bool CollisionShapeComponent::IsRegisteredInPhysicsScene() const noexcept {
        return m_shape_index != PhysicsScene::INVALID_INDEX;
    }

    uint32_t CollisionShapeComponent::GetPhysicsShapeIndex() const noexcept {
        return m_shape_index;
    }

    glm::vec3 CollisionShapeComponent::GetLocalCenterInParentSpace() const {
        return m_box_center;
    }

    glm::quat CollisionShapeComponent::GetLocalRotationInParentSpace() const {
        return m_box_rotation;
    }

    bool CollisionShapeComponent::TryAttachToAncestorRigidBody() {
        auto *scene = GetScene();
        auto *owner = GetParentGameObject();
        if (scene == nullptr || owner == nullptr) {
            return false;
        }

        auto *physics_scene = scene->GetPhysicsScene();
        if (physics_scene == nullptr || m_shape_index == PhysicsScene::INVALID_INDEX) {
            return false;
        }

        ObjectHandle current = owner->GetHandle();
        while (current.IsValid()) {
            auto *go = scene->GetGameObject(current);
            if (go == nullptr) {
                break;
            }

            for (const ComponentHandle &comp_handle : go->m_components) {
                if (auto *rigid = dynamic_cast<RigidBodyComponent *>(scene->GetComponent(comp_handle))) {
                    const uint32_t rigid_body_index = rigid->GetPhysicsRigidBodyIndex();
                    if (rigid_body_index != PhysicsScene::INVALID_INDEX) {
                        physics_scene->SetCollisionShapeRigidBody(m_shape_index, rigid_body_index);
                        return true;
                    }
                }
            }

            current = go->GetParent();
        }

        return false;
    }
} // namespace Engine

#include "__generated__/CollisionShapeComponent.h.inc"
