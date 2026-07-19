#include "CollisionShapeComponent.h"

#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/physics/PhysicsAdaptor.h>

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
        if (scene->GetPhysicsScene() == nullptr) return; // scene physics not enabled

        auto &adaptor = scene->GetPhysicsAdaptor();
        m_shape_index = adaptor.AllocateShapeSlot(GetHandle());
    }

    void CollisionShapeComponent::Init() {
        auto *scene = GetScene();
        auto *owner = GetParentGameObject();
        if (scene == nullptr || owner == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "CollisionShapeComponent init failed: missing scene or owner");
            return;
        }
        if (scene->GetPhysicsScene() == nullptr) return; // scene physics not enabled

        if (m_shape_index == PhysicsScene::INVALID_INDEX) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "CollisionShapeComponent init failed: shape not registered (Awake may have failed)"
            );
            return;
        }

        auto &adaptor = scene->GetPhysicsAdaptor();
        CollisionShapeDescriptor desc = BuildDescriptor(owner);
        adaptor.SubmitShape(m_shape_index, desc);
        TryAttachToAncestorRigidBody();
    }

    bool CollisionShapeComponent::IsRegisteredInPhysicsScene() const noexcept {
        return m_shape_index != PhysicsScene::INVALID_INDEX;
    }

    uint32_t CollisionShapeComponent::GetPhysicsShapeIndex() const noexcept {
        return m_shape_index;
    }

    glm::vec3 CollisionShapeComponent::GetLocalCenterInParentSpace() const {
        return m_center;
    }

    glm::quat CollisionShapeComponent::GetLocalRotationInParentSpace() const {
        return m_rotation;
    }

    CollisionShapeDescriptor CollisionShapeComponent::BuildDescriptor(GameObject *owner) {
        Transform world_transform = owner->GetWorldTransform();
        glm::vec3 world_center = world_transform.GetPosition() + world_transform.GetRotation() * m_center;
        glm::quat world_rotation = glm::normalize(world_transform.GetRotation() * m_rotation);

        CollisionShapeType effective_type = m_shape_type;
        glm::vec3 effective_feature = m_feature;

        if (m_shape_type == CollisionShapeType::Cylinder) {
            const glm::vec3 world_scale = world_transform.GetScale();
            if (glm::abs(world_scale.x - world_scale.y) > 1e-4f) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "CollisionShapeComponent: Cylinder shape has non-uniform XY scale "
                    "(scale=%.3f, %.3f, %.3f). Falling back to bounding box approximation.",
                    world_scale.x,
                    world_scale.y,
                    world_scale.z
                );
                const float r = m_feature.x;
                const float half_h = m_feature.y;
                effective_type = CollisionShapeType::Box;
                effective_feature = glm::vec3(
                    r * glm::abs(world_scale.x), r * glm::abs(world_scale.y), half_h * glm::abs(world_scale.z)
                );
            }
        }

        CollisionShapeDescriptor desc;
        desc.type = effective_type;
        desc.feature = effective_feature;
        desc.world_position = world_center;
        desc.world_rotation = world_rotation;
        desc.ignore_collision_shapes = m_ignore_collision_shapes;
        return desc;
    }

    bool CollisionShapeComponent::TryAttachToAncestorRigidBody() {
        auto *scene = GetScene();
        auto *owner = GetParentGameObject();
        if (scene == nullptr || owner == nullptr) {
            return false;
        }
        if (m_shape_index == PhysicsScene::INVALID_INDEX) {
            return false;
        }

        auto &adaptor = scene->GetPhysicsAdaptor();
        ObjectHandle current = owner->GetHandle();
        while (current.IsValid()) {
            const uint32_t rigid_body_index = adaptor.FindRigidBodyByObjectHandle(current);
            if (rigid_body_index != PhysicsScene::INVALID_INDEX) {
                adaptor.BindShapeToRigidBody(m_shape_index, rigid_body_index);
                return true;
            }

            auto *go = scene->GetGameObject(current);
            if (go == nullptr) {
                break;
            }
            current = go->GetParent();
        }

        return false;
    }
} // namespace Engine

#include "__generated__/CollisionShapeComponent.h.inc"
