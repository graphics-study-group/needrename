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
        Box = 0,
        Sphere = 1,
        Cylinder = 2
    };

    /**
     * @brief Packed GPU struct for a FixedJoint constraint definition.
     *
     * Static input data only — no runtime state. Lagrange multipliers are
     * managed separately by XPBDGpuSolver as SoA buffers.
     *
     * All spatial values are in COM-local space (converted from GO-local
     * by ConvertPendingJointUpdates).
     *
     * std430 layout: 48 bytes (3 × vec4 equivalent).
     */
    struct GpuFixedJoint {
        uint32_t obj1_index;
        uint32_t obj2_index;
        float compliance;
        float _pad;
        glm::vec4 initial_rel_pos_local; ///< q1_com_init⁻¹ * (pos2_com_init - pos1_com_init), COM-local
        glm::vec4 initial_rel_rotation;  ///< q1_com_init⁻¹ * q2_com_init
    };

    /**
     * @brief Packed GPU struct for a HingeJoint constraint definition.
     *
     * Static input data only — no runtime state. Lagrange multipliers are
     * managed separately by XPBDGpuSolver as SoA buffers.
     *
     * All spatial values are in COM-local space (converted from GO-local
     * by ConvertPendingJointUpdates).
     *
     * std430 layout: 80 bytes (5 × vec4 equivalent).
     */
    struct GpuHingeJoint {
        uint32_t obj1_index;
        uint32_t obj2_index;
        float compliance;
        float _pad;
        glm::vec4 hinge_axis_obj1;       ///< Hinge axis in obj1's COM-local frame.
        glm::vec4 hinge_anchor_obj1;     ///< Hinge anchor point in obj1's COM-local frame.
        glm::vec4 initial_rel_pos_local; ///< q1_com_init⁻¹ * (pos2_com_init - pos1_com_init), COM-local
        glm::vec4 initial_rel_rotation;  ///< q1_com_init⁻¹ * q2_com_init
    };

    static_assert(sizeof(GpuFixedJoint) == 48, "GpuFixedJoint must be 48 bytes (std430)");
    static_assert(sizeof(GpuHingeJoint) == 80, "GpuHingeJoint must be 80 bytes (std430)");

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
         * @param initial_world_position Initial world position (from GameObject transform).
         * @param initial_world_rotation Initial world rotation (from GameObject transform).
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
            const glm::vec3 &initial_world_position,
            const glm::quat &initial_world_rotation,
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
         * @param feature Shape type-dependent feature vec3 (Box: half-extents,
         *        Sphere: radius in x, Cylinder: radius in x, half-height in y).
         * @param shape_world_position Shape world position.
         * @param shape_world_rotation Shape world rotation.
         * @return Allocated shape index.
         */
        uint32_t RegisterCollisionShape(
            ComponentHandle component_handle,
            CollisionShapeType shape_type,
            const glm::vec3 &feature,
            const glm::vec3 &shape_world_position,
            const glm::quat &shape_world_rotation,
            const std::vector<ObjectHandle> &ignore_collision_objects = {}
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
         * @param feature Shape type-dependent feature vec3 (Box: half-extents,
         *        Sphere: radius in x, Cylinder: radius in x, half-height in y).
         * @param shape_world_position Shape world position.
         * @param shape_world_rotation Shape world rotation.
         */
        void UpdateCollisionShapeGeometry(
            uint32_t shape_index,
            CollisionShapeType shape_type,
            const glm::vec3 &feature,
            const glm::vec3 &shape_world_position,
            const glm::quat &shape_world_rotation,
            const std::vector<ObjectHandle> &ignore_collision_objects = {}
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
         * @brief Set a manually-defined inertia tensor for a rigid body.
         *
         * When set, RecalculateRigidBodyState skips automatic volume-weighted
         * inertia computation and uses the provided tensor directly.
         *
         * @param rigid_body_index Rigid body index.
         * @param inertia 3x3 inertia tensor.
         */
        void SetRigidBodyManualInertia(uint32_t rigid_body_index, const glm::mat3 &inertia);

        /**
         * @brief Set a manually-defined center-of-mass offset for a rigid body.
         *
         * The offset is expressed in GO-local space. Only applied when
         * manual inertia is also enabled (m_use_manual_inertia_com).
         *
         * @param rigid_body_index Rigid body index.
         * @param com_offset COM offset in GO-local space.
         */
        void SetRigidBodyManualCenterOfMass(uint32_t rigid_body_index, const glm::vec3 &com_offset);

        /**
         * @brief Get the center-of-mass offset for a rigid body.
         *
         * The offset is expressed in GO-local space: COM_world =
         * GO_world + rot(GO_rot, offset).
         *
         * @param rigid_body_index Rigid body index.
         * @return COM offset in GO-local space, or zero vector if invalid.
         */
        glm::vec3 GetRigidBodyCenterOffsetLocal(uint32_t rigid_body_index) const;

        /**
         * @brief Allocate an empty FixedJoint slot in the joints vector.
         *
         * Used by PhysicsConstraintComponent::Awake to reserve a slot before
         * rigid body indices are known. The slot is filled by UpdateFixedJoint
         * during Init.
         *
         * @return Allocated joint index.
         */
        uint32_t AllocateFixedJoint();

        /**
         * @brief Fill an allocated FixedJoint slot with resolved data.
         *
         * The passed values are in GO-local space when called from
         * PhysicsConstraintComponent::Init. Conversion to COM-local space is
         * deferred to InitializePendingRigidBodies.
         *
         * @param joint_idx Joint index returned by AllocateFixedJoint.
         * @param obj1_index Rigid body index of the owning object.
         * @param obj2_index Rigid body index of the second object.
         * @param compliance Joint compliance parameter.
         * @param initial_rel_pos_local Initial relative position in obj1's GO-local frame.
         * @param initial_rel_rotation Initial relative rotation quaternion.
         */
        void UpdateFixedJoint(
            uint32_t joint_idx,
            uint32_t obj1_index,
            uint32_t obj2_index,
            float compliance,
            const glm::vec3 &initial_rel_pos_local,
            const glm::quat &initial_rel_rotation
        );

        /**
         * @brief Allocate an empty HingeJoint slot in the joints vector.
         *
         * Used by PhysicsConstraintComponent::Awake to reserve a slot.
         *
         * @return Allocated joint index.
         */
        uint32_t AllocateHingeJoint();

        /**
         * @brief Fill an allocated HingeJoint slot with resolved data.
         *
         * The passed values are in GO-local space when called from
         * PhysicsConstraintComponent::Init. Conversion to COM-local space is
         * deferred to InitializePendingRigidBodies.
         *
         * @param joint_idx Joint index returned by AllocateHingeJoint.
         * @param obj1_index Rigid body index of the owning object.
         * @param obj2_index Rigid body index of the second object.
         * @param compliance Joint compliance parameter.
         * @param hinge_axis_obj1 Hinge axis in obj1's GO-local frame.
         * @param hinge_anchor_obj1 Hinge anchor point in obj1's GO-local frame.
         * @param initial_rel_pos_local q1_init⁻¹ * (pos2_init - pos1_init) in GO-local.
         * @param initial_rel_rotation q1_init⁻¹ * q2_init.
         */
        void UpdateHingeJoint(
            uint32_t joint_idx,
            uint32_t obj1_index,
            uint32_t obj2_index,
            float compliance,
            const glm::vec3 &hinge_axis_obj1,
            const glm::vec3 &hinge_anchor_obj1,
            const glm::vec3 &initial_rel_pos_local,
            const glm::quat &initial_rel_rotation
        );

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
            const ComputeBuffer *rigid_body_inverse_inertia{};
            const ComputeBuffer *rigid_body_linear_velocity{};
            const ComputeBuffer *rigid_body_angular_velocity{};
            const ComputeBuffer *rigid_body_external_force{};
            const ComputeBuffer *rigid_body_external_torque{};

            const ComputeBuffer *shape_alive{};
            const ComputeBuffer *shape_type{};
            const ComputeBuffer *shape_bound_rigid_body{};
            const ComputeBuffer *shape_feature{};
            const ComputeBuffer *shape_local_position{};
            const ComputeBuffer *shape_local_rotation{};
            const ComputeBuffer *shape_world_position{};
            const ComputeBuffer *shape_world_rotation{};

            const ComputeBuffer *rigid_body_shape_offset{};
            const ComputeBuffer *rigid_body_shape_count{};
            const ComputeBuffer *flattened_shape_indices{};

            const ComputeBuffer *model_matrices{};

            const ComputeBuffer *shape_filter_offset{};
            const ComputeBuffer *shape_filter_count{};
            const ComputeBuffer *shape_filter_data{};

            const ComputeBuffer *gpu_fixed_joints{};
            const ComputeBuffer *gpu_hinge_joints{};

            uint32_t rigid_body_slot_count{0};
            uint32_t shape_slot_count{0};
            uint32_t fixed_joint_count{0};
            uint32_t hinge_joint_count{0};
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

        /**
         * @brief Upload current GameObject world transform for a rigid body.
         *
         * This stores a temporary GO-world placeholder in center_world_position.
         * It is overwritten by RecalculateRigidBodyState during the next
         * InitializePendingRigidBodies call, which computes the actual COM
         * position and center offset.
         *
         * @param rigid_body_index Rigid body index.
         * @param world_position Current GO world position (temporary; will be replaced by COM).
         * @param world_rotation Current GO world rotation.
         */
        void SetRigidBodyTransform(
            uint32_t rigid_body_index, const glm::vec3 &world_position, const glm::quat &world_rotation
        );

        /**
         * @brief Enable or disable GPU simulation for this scene.
         *
         * When disabled, the XPBD compute passes will skip dispatch
         * and all model_matrix_active flags are cleared.
         *
         * @param enabled True to enable simulation, false to pause.
         */
        void SetSimulationEnabled(bool enabled);

        /**
         * @brief Check whether GPU simulation is enabled.
         *
         * @return True if simulation is enabled.
         */
        bool IsSimulationEnabled() const noexcept;

        /**
         * @brief Set whether this rigid body's model matrix SSBO slot is active.
         *
         * When active, PreRenderUpdate sets model_mat_index for descendant renderers.
         * When inactive, renderers use TransformComponent push-constants instead.
         *
         * @param rigid_body_index Rigid body index.
         * @param active True to enable SSBO-driven rendering.
         */
        void SetModelMatrixActive(uint32_t rigid_body_index, bool active);

        /**
         * @brief Check whether this rigid body's model matrix SSBO slot is active.
         *
         * @param rigid_body_index Rigid body index.
         * @return True if the SSBO path is active for this rigid body.
         */
        bool IsModelMatrixActive(uint32_t rigid_body_index) const noexcept;

        /**
         * @brief Resolve pending collision filter ObjectHandles to shape indices.
         *
         * Called once after all GameObjects have been Awake'd and before the
         * first simulation step.  Resolves each ObjectHandle stored in
         * m_pending_filter_handles to the shape index of the target object's
         * directly-attached CollisionShapeComponent, enforces symmetry, and
         * uploads filter data to GPU buffers.
         *
         * @param scene Scene used to look up GameObjects by ObjectHandle.
         */
        void ResolveCollisionFilters(class Scene &scene);

    private:
        void AddShapeToRigidBodyMap(uint32_t rigid_body_index, uint32_t shape_index);
        void RemoveShapeFromRigidBodyMap(uint32_t rigid_body_index, uint32_t shape_index);
        void RecalculateRigidBodyState(uint32_t rigid_body_index);
        void RefreshGpuBuffers(RenderSystem &render_system);
        void ConvertPendingJointUpdates();

        uint32_t m_scene_id{0};

        bool m_simulation_enabled = false;

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
        std::vector<glm::mat4> m_rigid_body_inverse_inertia{};
        // Manual inertia/COM override (not GPU-synced — used in RecalculateRigidBodyState)
        std::vector<bool> m_rigid_body_use_manual_inertia_com{};
        std::vector<glm::mat3> m_rigid_body_manual_inertia{};
        std::vector<glm::vec3> m_rigid_body_manual_center_of_mass{};
        std::vector<glm::vec4> m_rigid_body_linear_velocity{};
        std::vector<glm::vec4> m_rigid_body_angular_velocity{};
        std::vector<glm::vec4> m_rigid_body_external_force{};
        std::vector<glm::vec4> m_rigid_body_external_torque{};
        std::vector<bool> m_rigid_body_need_init{};
        std::deque<uint32_t> m_rigid_body_init_queue{};
        std::unordered_map<uint32_t, std::vector<uint32_t>> m_rigid_body_to_shapes{};
        std::vector<bool> m_rigid_body_model_matrix_active{};

        std::vector<uint32_t> m_shape_to_rigid_body{};
        std::vector<uint32_t> m_shape_type{};
        std::vector<glm::vec4> m_shape_feature{};
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
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_inverse_inertia{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_linear_velocity{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_angular_velocity{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_external_force{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_external_torque{};

        std::unique_ptr<ComputeBuffer> m_gpu_shape_alive{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_type{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_bound_rigid_body{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_feature{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_local_position{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_local_rotation{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_world_position{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_world_rotation{};

        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_shape_offset{};
        std::unique_ptr<ComputeBuffer> m_gpu_rigid_body_shape_count{};
        std::unique_ptr<ComputeBuffer> m_gpu_flattened_shape_indices{};

        std::unique_ptr<ComputeBuffer> m_gpu_model_matrices{};

        // Joint constraint storage (static input data, AoS per joint type).
        std::vector<GpuFixedJoint> m_fixed_joints{};
        std::vector<GpuHingeJoint> m_hinge_joints{};
        std::unique_ptr<ComputeBuffer> m_gpu_fixed_joints{};
        std::unique_ptr<ComputeBuffer> m_gpu_hinge_joints{};

        // Pending joint updates — stored in GO-local space during Init,
        // converted to COM-local by ConvertPendingJointUpdates.
        struct PendingFixedJointUpdate {
            uint32_t joint_idx;
            uint32_t obj1_index;
            uint32_t obj2_index;
            float compliance;
            glm::vec3 initial_rel_pos_local; ///< GO-local
            glm::quat initial_rel_rotation;
        };
        std::vector<PendingFixedJointUpdate> m_pending_fixed_joint_updates{};

        struct PendingHingeJointUpdate {
            uint32_t joint_idx;
            uint32_t obj1_index;
            uint32_t obj2_index;
            float compliance;
            glm::vec3 hinge_axis_obj1;      ///< GO-local
            glm::vec3 hinge_anchor_obj1;    ///< GO-local
            glm::vec3 initial_rel_pos_local; ///< GO-local
            glm::quat initial_rel_rotation;
        };
        std::vector<PendingHingeJointUpdate> m_pending_hinge_joint_updates{};

        // Collision filter data — CPU-side.
        std::vector<std::vector<ObjectHandle>> m_pending_filter_handles{};
        std::vector<uint32_t> m_shape_filter_offset{};
        std::vector<uint32_t> m_shape_filter_count{};
        std::vector<uint32_t> m_shape_filter_data{};

        // Collision filter data — GPU buffers.
        std::unique_ptr<ComputeBuffer> m_gpu_shape_filter_offset{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_filter_count{};
        std::unique_ptr<ComputeBuffer> m_gpu_shape_filter_data{};
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED
