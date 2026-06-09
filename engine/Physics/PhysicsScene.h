#ifndef ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED
#define ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED

#include <Framework/world/Handle.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {
    class ComputeBuffer;
    class RenderSystem;

    /**
     * @brief Enumerates supported collision shape kinds.
     *
     * This is used by component-side editable data and by PhysicsScene shape
     * records to select the active feature payload.
     */
    enum class REFL_SER_CLASS() CollisionShapeType {
        Box = 0
    };

    /**
     * @brief Per-engine-scene physics storage.
     *
     * PhysicsScene stores rigid body and collision shape data using
     * data-oriented arrays, plus mapping tables between engine handles and
     * internal indices.
     */
    class PhysicsScene {
    public:
        static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

        /**
         * @brief Construct a physics scene.
         *
         * @param scene_id Engine scene ID that owns this physics scene.
         */
        explicit PhysicsScene(uint32_t scene_id);

        /**
         * @brief Destroy the physics scene.
         */
        ~PhysicsScene();

        /**
         * @brief Disable copy construction.
         */
        PhysicsScene(const PhysicsScene &) = delete;

        /**
         * @brief Disable copy assignment.
         *
         * @return This object.
         */
        PhysicsScene &operator=(const PhysicsScene &) = delete;

        /**
         * @brief Disable move construction.
         */
        PhysicsScene(PhysicsScene &&) = delete;

        /**
         * @brief Disable move assignment.
         *
         * @return This object.
         */
        PhysicsScene &operator=(PhysicsScene &&) = delete;

        /**
         * @brief Get owning engine scene ID.
         *
         * @return Scene ID used as the key in PhysicsSystem.
         */
        uint32_t GetSceneID() const noexcept;

        /**
         * @brief Clear all rigid body and shape records.
         *
         * This keeps the scene instance alive while resetting all storage.
         */
        void Clear();

        /**
         * @brief Register one rigid body slot.
         *
         * @param owner_object Engine object handle of this rigid body.
         * @param mass Rigid body mass.
         * @param static_friction Static friction coefficient.
         * @param dynamic_friction Dynamic friction coefficient.
         * @param restitution Restitution coefficient.
         * @param is_kinematic Whether this rigid body is kinematic.
         * @param linear_velocity Initial linear velocity.
         * @param angular_velocity_axis_angle Initial angular velocity (axis-angle vector).
         * @param external_force External force accumulator.
         * @param external_torque External torque accumulator.
         * @return Allocated rigid body index.
         */
        uint32_t RegisterRigidBody(
            ObjectHandle owner_object,
            float mass,
            float static_friction,
            float dynamic_friction,
            float restitution,
            bool is_kinematic,
            const glm::vec3 &linear_velocity,
            const glm::vec3 &angular_velocity_axis_angle,
            const glm::vec3 &external_force,
            const glm::vec3 &external_torque
        );

        /**
         * @brief Unregister one rigid body slot.
         *
         * @param rigid_body_index Rigid body index to unregister.
         */
        void UnregisterRigidBody(uint32_t rigid_body_index);

        /**
         * @brief Register one collision shape slot.
         *
         * @param component_handle Engine component handle of this shape.
         * @param shape_type Shape type enum.
         * @param half_extents Box half extents.
         * @param shape_world_position Shape world position.
         * @param shape_world_rotation Shape world rotation.
         * @return Allocated shape index.
         */
        uint32_t RegisterCollisionShape(
            ComponentHandle component_handle,
            CollisionShapeType shape_type,
            const glm::vec3 &half_extents,
            const glm::vec3 &shape_world_position,
            const glm::quat &shape_world_rotation
        );

        /**
         * @brief Unregister one collision shape slot.
         *
         * @param shape_index Shape index to unregister.
         */
        void UnregisterCollisionShape(uint32_t shape_index);

        /**
         * @brief Set owning rigid body index of one shape.
         *
         * @param shape_index Shape index.
         * @param rigid_body_index Rigid body index or INVALID_INDEX.
         */
        void SetCollisionShapeRigidBody(uint32_t shape_index, uint32_t rigid_body_index);

        /**
         * @brief Update shape geometry and world pose.
         *
         * This is called by component-side editable data sync. If the shape is
         * currently bound to a rigid body, that rigid body will be marked for
         * reinitialization.
         *
         * @param shape_index Shape index.
         * @param shape_type Shape type enum.
         * @param half_extents Box half extents.
         * @param shape_world_position Shape world position.
         * @param shape_world_rotation Shape world rotation.
         */
        void UpdateCollisionShapeGeometry(
            uint32_t shape_index,
            CollisionShapeType shape_type,
            const glm::vec3 &half_extents,
            const glm::vec3 &shape_world_position,
            const glm::quat &shape_world_rotation
        );

        /**
         * @brief Update stored rigid body properties.
         *
         * @param rigid_body_index Rigid body index.
         * @param mass Rigid body mass.
         * @param static_friction Static friction coefficient.
         * @param dynamic_friction Dynamic friction coefficient.
         * @param restitution Restitution coefficient.
         * @param is_kinematic Whether this rigid body is kinematic.
         * @param linear_velocity Linear velocity.
         * @param angular_velocity_axis_angle Angular velocity (axis-angle vector).
         * @param external_force External force accumulator.
         * @param external_torque External torque accumulator.
         */
        void SetRigidBodyProperties(
            uint32_t rigid_body_index,
            float mass,
            float static_friction,
            float dynamic_friction,
            float restitution,
            bool is_kinematic,
            const glm::vec3 &linear_velocity,
            const glm::vec3 &angular_velocity_axis_angle,
            const glm::vec3 &external_force,
            const glm::vec3 &external_torque
        );

        /**
         * @brief Queue one rigid body for deferred initialization.
         *
         * @param rigid_body_index Rigid body index.
         */
        void EnqueueRigidBodyInitialization(uint32_t rigid_body_index);

        /**
         * @brief Initialize all queued rigid bodies.
         *
         * This recalculates center of mass, inertia tensor, and shape local
         * poses for affected rigid bodies, then refreshes the GPU SoA mirror.
         */
        void InitializePendingRigidBodies(RenderSystem &render_system);

        struct PhysicsGpuBuffers {
            const ComputeBuffer *rigid_body_alive{};
            const ComputeBuffer *rigid_body_mass{};
            const ComputeBuffer *rigid_body_static_friction{};
            const ComputeBuffer *rigid_body_dynamic_friction{};
            const ComputeBuffer *rigid_body_restitution{};
            const ComputeBuffer *rigid_body_is_kinematic{};
            const ComputeBuffer *rigid_body_center_world_position{};
            const ComputeBuffer *rigid_body_center_world_rotation{};
            const ComputeBuffer *rigid_body_center_offset_local_position{};
            const ComputeBuffer *rigid_body_inertia{};
            const ComputeBuffer *rigid_body_linear_velocity{};
            const ComputeBuffer *rigid_body_angular_velocity{};
            const ComputeBuffer *rigid_body_external_force{};
            const ComputeBuffer *rigid_body_external_torque{};

            const ComputeBuffer *shape_alive{};
            const ComputeBuffer *shape_type{};
            const ComputeBuffer *shape_bound_rigid_body{};
            const ComputeBuffer *shape_half_extents{};
            const ComputeBuffer *shape_local_position{};
            const ComputeBuffer *shape_local_rotation{};
            const ComputeBuffer *shape_world_position{};
            const ComputeBuffer *shape_world_rotation{};

            const ComputeBuffer *rigid_body_shape_offset{};
            const ComputeBuffer *rigid_body_shape_count{};
            const ComputeBuffer *flattened_shape_indices{};

            const ComputeBuffer *model_matrices{};

            uint32_t rigid_body_slot_count{0};
            uint32_t shape_slot_count{0};
        };

        PhysicsGpuBuffers GetGpuBuffers() const noexcept;

        /**
         * @brief Check whether a rigid body index is alive.
         *
         * @param rigid_body_index Rigid body index.
         * @return True if the index exists and is alive.
         */
        bool IsRigidBodyIndexValid(uint32_t rigid_body_index) const;

        /**
         * @brief Check whether a shape index is alive.
         *
         * @param shape_index Shape index.
         * @return True if the index exists and is alive.
         */
        bool IsShapeIndexValid(uint32_t shape_index) const;

        /**
         * @brief Find rigid body index by engine object handle.
         *
         * @param object_handle Engine object handle.
         * @return Rigid body index, or INVALID_INDEX if not found.
         */
        uint32_t FindRigidBodyByObjectHandle(ObjectHandle object_handle) const;

        /**
         * @brief Find shape index by engine component handle.
         *
         * @param component_handle Engine component handle.
         * @return Shape index, or INVALID_INDEX if not found.
         */
        uint32_t FindShapeByComponentHandle(ComponentHandle component_handle) const;

        /**
         * @brief Log all internal state for debugging.
         */
        void DebugPrint() const;

    private:
        void AddShapeToRigidBodyMap(uint32_t rigid_body_index, uint32_t shape_index);
        void RemoveShapeFromRigidBodyMap(uint32_t rigid_body_index, uint32_t shape_index);
        void RecalculateRigidBodyState(uint32_t rigid_body_index);
        void RefreshGpuBuffers(RenderSystem &render_system);

        uint32_t m_scene_id{0};

        std::vector<uint32_t> m_rigid_body_alive{};
        std::vector<uint32_t> m_shape_alive{};

        std::vector<ObjectHandle> m_rigid_body_to_object{};
        std::unordered_map<ObjectHandle, uint32_t> m_object_to_rigid_body{};

        std::unordered_map<ComponentHandle, uint32_t> m_shape_component_to_index{};
        std::vector<ComponentHandle> m_shape_index_to_component{};

        std::vector<float> m_rigid_body_mass{};
        std::vector<float> m_rigid_body_static_friction{};
        std::vector<float> m_rigid_body_dynamic_friction{};
        std::vector<float> m_rigid_body_restitution{};
        std::vector<uint32_t> m_rigid_body_is_kinematic{};
        std::vector<glm::vec4> m_rigid_body_center_world_position{};
        std::vector<glm::vec4> m_rigid_body_center_world_rotation{};
        std::vector<glm::vec4> m_rigid_body_center_offset_local_position{};
        std::vector<glm::mat4> m_rigid_body_inertia{};
        std::vector<glm::vec4> m_rigid_body_linear_velocity{};
        std::vector<glm::vec4> m_rigid_body_angular_velocity{};
        std::vector<glm::vec4> m_rigid_body_external_force{};
        std::vector<glm::vec4> m_rigid_body_external_torque{};
        std::vector<bool> m_rigid_body_need_init{};
        std::deque<uint32_t> m_rigid_body_init_queue{};
        std::unordered_map<uint32_t, std::vector<uint32_t>> m_rigid_body_to_shapes{};

        std::vector<uint32_t> m_shape_to_rigid_body{};
        std::vector<uint32_t> m_shape_type{};
        std::vector<glm::vec4> m_shape_half_extents{};
        std::vector<glm::vec4> m_shape_position{};
        std::vector<glm::vec4> m_shape_rotation{};
        std::vector<glm::vec4> m_shape_world_position{};
        std::vector<glm::vec4> m_shape_world_rotation{};

        uint32_t m_gpu_rigid_body_slot_count{0};
        uint32_t m_gpu_shape_slot_count{0};
        uint32_t m_gpu_flattened_shape_index_count{0};

        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_alive{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_mass{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_static_friction{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_dynamic_friction{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_restitution{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_is_kinematic{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_center_world_position{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_center_world_rotation{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_center_offset_local_position{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_inertia{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_linear_velocity{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_angular_velocity{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_external_force{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_external_torque{};

        std::unique_ptr<ComputeBuffer> m_gpu_shape_alive{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_type{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_bound_rigid_body{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_half_extents{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_local_position{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_local_rotation{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_world_position{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_world_rotation{};

        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_shape_offset{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_shape_count{};
        std::unique_ptr<ComputeBuffer> m_gpu_flattened_shape_indices{};

        std::unique_ptr<ComputeBuffer> m_gpu_model_matrices{};
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED
