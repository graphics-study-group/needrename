#ifndef FRAMEWORK_WORLD_PHYSICS_PHYSICSADAPTOR_INCLUDED
#define FRAMEWORK_WORLD_PHYSICS_PHYSICSADAPTOR_INCLUDED

#include "PhysicsDescriptors.h"

#include <Framework/world/Handle.h>
#include <Physics/PhysicsScene.h>

#include <glm.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {
    class Scene;
    class RenderSystem;

    class PhysicsAdaptor {
    public:
        PhysicsAdaptor(PhysicsScene &physics_scene, Scene &scene);
        ~PhysicsAdaptor();

        PhysicsAdaptor(const PhysicsAdaptor &) = delete;
        PhysicsAdaptor &operator=(const PhysicsAdaptor &) = delete;
        PhysicsAdaptor(PhysicsAdaptor &&) = delete;
        PhysicsAdaptor &operator=(PhysicsAdaptor &&) = delete;

        uint32_t AllocateSlot(ObjectHandle owner);
        uint32_t AllocateShapeSlot(ComponentHandle owner);
        uint32_t AllocateFixedJoint();
        uint32_t AllocateHingeJoint();

        void SubmitRigidBody(uint32_t index, const RigidBodyDescriptor &desc);
        void SubmitShape(uint32_t index, const CollisionShapeDescriptor &desc);
        void SubmitJoint(uint32_t joint_idx, const JointSubmitData &data);

        void BindShapeToRigidBody(uint32_t shape_idx, uint32_t rb_idx);

        void Flush(RenderSystem &render_system);

        uint32_t FindRigidBodyByObjectHandle(ObjectHandle handle) const;
        glm::vec3 GetComOffsetLocal(uint32_t rb_idx) const;

        bool IsPhysicsActive() const;
        void SetPhysicsActive(bool active);

        PhysicsScene &GetPhysicsScene();

    private:
        PhysicsScene &m_physics_scene;
        Scene &m_scene;

        std::unordered_map<ObjectHandle, uint32_t> m_object_to_rigid_body{};
        std::unordered_map<uint32_t, ObjectHandle> m_rigid_body_to_object{};
        std::unordered_map<ComponentHandle, uint32_t> m_shape_component_to_index{};
        std::unordered_map<uint32_t, std::vector<uint32_t>> m_rigid_body_to_shapes{};
        std::unordered_map<uint32_t, uint32_t> m_shape_to_rigid_body{};

        std::unordered_map<uint32_t, RigidBodyDescriptor> m_pending_rigid_bodies{};
        std::unordered_map<uint32_t, CollisionShapeDescriptor> m_pending_shapes{};
        std::unordered_map<uint32_t, JointSubmitData> m_pending_joints{};

        std::unordered_map<uint32_t, glm::vec3> m_com_offsets{};

        std::unordered_map<uint32_t, std::vector<uint32_t>> m_resolved_filters{};

        bool m_physics_active{false};
    };
} // namespace Engine

#endif // FRAMEWORK_WORLD_PHYSICS_PHYSICSADAPTOR_INCLUDED
