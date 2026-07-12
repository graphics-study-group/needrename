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

    /**
     * @brief Bridges GameObject-space physics descriptors to the COM-space PhysicsScene.
     *
     * PhysicsAdaptor is owned by Scene and serves as the sole translation layer
     * between the GO/Component world and the COM-space physics storage. Components
     * submit GO-space descriptors during Init; the Adaptor computes COM, inertia,
     * and coordinate conversions during Flush, then submits COM-space descriptors
     * to PhysicsScene.
     */
    class PhysicsAdaptor {
    public:
        /**
         * @brief Construct the adaptor bound to a physics scene and owning scene.
         *
         * @param physics_scene The COM-space physics scene to submit results to.
         * @param scene         The owning Scene, used for collision filter resolution.
         */
        PhysicsAdaptor(PhysicsScene &physics_scene, Scene &scene);

        /**
         * @brief Destroy the adaptor.
         */
        ~PhysicsAdaptor();

        PhysicsAdaptor(const PhysicsAdaptor &) = delete;
        PhysicsAdaptor &operator=(const PhysicsAdaptor &) = delete;
        PhysicsAdaptor(PhysicsAdaptor &&) = delete;
        PhysicsAdaptor &operator=(PhysicsAdaptor &&) = delete;

        /**
         * @brief Allocate a rigid body slot associated with a GameObject.
         *
         * Idempotent: repeated calls with the same handle return the existing index.
         *
         * @param owner The ObjectHandle of the owning GameObject.
         * @return The allocated rigid body slot index.
         */
        uint32_t AllocateSlot(ObjectHandle owner);

        /**
         * @brief Allocate a collision shape slot associated with a Component.
         *
         * Idempotent: repeated calls with the same handle return the existing index.
         *
         * @param owner The ComponentHandle of the owning CollisionShapeComponent.
         * @return The allocated shape slot index.
         */
        uint32_t AllocateShapeSlot(ComponentHandle owner);

        /**
         * @brief Allocate a new fixed joint slot.
         *
         * The slot is initialized with INVALID_INDEX placeholders; actual data
         * is filled during Flush.
         *
         * @return The allocated joint slot index.
         */
        uint32_t AllocateFixedJoint();

        /**
         * @brief Allocate a new hinge joint slot.
         *
         * @return The allocated joint slot index.
         */
        uint32_t AllocateHingeJoint();

        /**
         * @brief Submit a GO-space rigid body descriptor for deferred processing.
         *
         * The descriptor is stored in the pending map. During Flush, COM and inertia
         * are computed and a COM-space descriptor is submitted to PhysicsScene.
         *
         * @param index The rigid body slot index.
         * @param desc  The GO-space descriptor built from component fields.
         */
        void SubmitRigidBody(uint32_t index, const RigidBodyDescriptor &desc);

        /**
         * @brief Submit a GO-space collision shape descriptor for deferred processing.
         *
         * @param index The shape slot index.
         * @param desc  The GO-space descriptor built from component fields.
         */
        void SubmitShape(uint32_t index, const CollisionShapeDescriptor &desc);

        /**
         * @brief Submit joint submit data for deferred processing.
         *
         * @param joint_idx The joint slot index.
         * @param data      The joint submit data (fixed or hinge).
         */
        void SubmitJoint(uint32_t joint_idx, const JointSubmitData &data);

        /**
         * @brief Bind a collision shape to a rigid body.
         *
         * Calling with rb_idx = INVALID_INDEX unbinds the shape.
         * The binding is communicated to PhysicsScene via CollisionShapeComDescriptor during Flush.
         *
         * @param shape_idx The shape slot index.
         * @param rb_idx    The rigid body slot index, or INVALID_INDEX to unbind.
         */
        void BindShapeToRigidBody(uint32_t shape_idx, uint32_t rb_idx);

        /**
         * @brief Process all pending descriptors and submit COM-space results to PhysicsScene.
         *
         * Pipeline: resolve collision filters → compute COM + inertia for pending bodies →
         * submit shape COM descriptors → convert joints (GO→COM) → submit joint COM descriptors →
         * clear pending maps → sync GPU buffers.
         *
         * @param render_system The render system for GPU buffer synchronization.
         */
        void Flush(RenderSystem &render_system);

        /**
         * @brief Find the rigid body index associated with a GameObject handle.
         *
         * @param handle The GameObject's ObjectHandle.
         * @return The rigid body index, or INVALID_INDEX if not found.
         */
        uint32_t FindRigidBodyByObjectHandle(ObjectHandle handle) const;

        /**
         * @brief Get the GO→COM center-of-mass offset for a rigid body.
         *
         * The offset is computed during Flush and cached for subsequent queries.
         * Expressed in GO-local space.
         *
         * @param rb_idx The rigid body slot index.
         * @return The COM offset vector in GO-local space, or zero if not computed.
         */
        glm::vec3 GetComOffsetLocal(uint32_t rb_idx) const;

        /**
         * @brief Check whether physics simulation is active.
         *
         * When active, RendererComponents follow COM-driven model matrices.
         * When inactive, RendererComponents use their GO world transform directly.
         *
         * @return true if physics is active.
         */
        bool IsPhysicsActive() const;

        /**
         * @brief Enable or disable physics simulation.
         *
         * @param active true to enable COM-following rendering, false to disable.
         */
        void SetPhysicsActive(bool active);

        /**
         * @brief Get a reference to the underlying COM-space PhysicsScene.
         *
         * Used by solver registration and for direct GPU buffer access.
         *
         * @return Reference to the bound PhysicsScene.
         */
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
