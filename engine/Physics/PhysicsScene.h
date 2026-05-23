#ifndef ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED
#define ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED

#include <Framework/world/Handle.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <unordered_map>
#include <vector>

namespace Engine {
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
     * @brief Box collision feature data.
     *
     * The box is represented by half extents and a local pose relative to the
     * owning shape transform frame.
     */
    struct REFL_SER_CLASS(REFL_WHITELIST) CollisionBoxFeature {
        REFL_SER_SIMPLE_STRUCT(CollisionBoxFeature)

        REFL_SER_ENABLE glm::vec3 m_half_extents{0.5f, 0.5f, 0.5f};
        REFL_SER_ENABLE glm::vec3 m_center{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::quat m_rotation{0.0f, 0.0f, 0.0f, 1.0f};
    };

    /**
     * @brief Editable rigid body simulation properties.
     *
     * This struct stores material and motion related fields that are mirrored
     * from RigidBodyComponent into PhysicsScene arrays.
     */
    struct REFL_SER_CLASS(REFL_WHITELIST) RigidBodyPhysicsProperties {
        REFL_SER_SIMPLE_STRUCT(RigidBodyPhysicsProperties)

        REFL_SER_ENABLE float m_mass{1.0f};
        REFL_SER_ENABLE float m_static_friction{0.5f};
        REFL_SER_ENABLE float m_dynamic_friction{0.5f};
        REFL_SER_ENABLE float m_restitution{0.0f};
        REFL_SER_ENABLE bool m_is_kinematic{false};
        REFL_SER_ENABLE glm::vec3 m_linear_velocity{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_angular_velocity_axis_angle{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_external_force{0.0f, 0.0f, 0.0f};
        REFL_SER_ENABLE glm::vec3 m_external_torque{0.0f, 0.0f, 0.0f};
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
         * @param props Initial rigid body properties.
         * @return Allocated rigid body index.
         */
        uint32_t RegisterRigidBody(ObjectHandle owner_object, const RigidBodyPhysicsProperties &props);

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
         * @param box_feature Box feature payload.
         * @param world_position World-space shape position fallback.
         * @param world_rotation World-space shape rotation fallback.
         * @return Allocated shape index.
         */
        uint32_t RegisterCollisionShape(
            ComponentHandle component_handle,
            CollisionShapeType shape_type,
            const CollisionBoxFeature &box_feature,
            const glm::vec3 &world_position,
            const glm::quat &world_rotation
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
         * @brief Set shape local pose in rigid body center frame.
         *
         * @param shape_index Shape index.
         * @param position Local position relative to center frame.
         * @param rotation Local rotation relative to center frame.
         */
        void SetCollisionShapeLocalToCenter(uint32_t shape_index, const glm::vec3 &position, const glm::quat &rotation);

        /**
         * @brief Set rigid body center-of-mass state.
         *
         * @param rigid_body_index Rigid body index.
         * @param center_world_position Center world position.
         * @param center_world_rotation Center world rotation.
         * @param center_local_position_offset Center offset in root-local space.
         * @param center_local_rotation_offset Center rotation offset in root-local space.
         */
        void SetRigidBodyCenterState(
            uint32_t rigid_body_index,
            const glm::vec3 &center_world_position,
            const glm::quat &center_world_rotation,
            const glm::vec3 &center_local_position_offset,
            const glm::quat &center_local_rotation_offset
        );

        /**
         * @brief Set shape index range metadata for one rigid body.
         *
         * @param rigid_body_index Rigid body index.
         * @param shape_start Minimum shape index in this body.
         * @param shape_count Number of shapes associated with this body.
         */
        void SetRigidBodyShapeRange(uint32_t rigid_body_index, uint32_t shape_start, uint32_t shape_count);

        /**
         * @brief Update stored rigid body properties.
         *
         * @param rigid_body_index Rigid body index.
         * @param props New property values.
         */
        void SetRigidBodyProperties(uint32_t rigid_body_index, const RigidBodyPhysicsProperties &props);

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
        uint32_t m_scene_id{0};

        std::vector<bool> m_rigid_body_alive{};
        std::vector<bool> m_shape_alive{};

        std::vector<ObjectHandle> m_rigid_body_to_object{};
        std::unordered_map<ObjectHandle, uint32_t> m_object_to_rigid_body{};

        std::unordered_map<ComponentHandle, uint32_t> m_shape_component_to_index{};
        std::vector<ComponentHandle> m_shape_index_to_component{};

        std::vector<RigidBodyPhysicsProperties> m_rigid_body_properties{};
        std::vector<glm::vec3> m_rigid_body_center_world_position{};
        std::vector<glm::quat> m_rigid_body_center_world_rotation{};
        std::vector<glm::vec3> m_rigid_body_center_local_position_offset{};
        std::vector<glm::quat> m_rigid_body_center_local_rotation_offset{};
        std::vector<glm::vec3> m_rigid_body_linear_velocity{};
        std::vector<glm::vec3> m_rigid_body_angular_velocity_axis_angle{};
        std::vector<glm::vec3> m_rigid_body_external_force{};
        std::vector<glm::vec3> m_rigid_body_external_torque{};
        std::vector<uint32_t> m_rigid_body_shape_start{};
        std::vector<uint32_t> m_rigid_body_shape_count{};

        std::vector<uint32_t> m_shape_to_rigid_body{};
        std::vector<CollisionShapeType> m_shape_type{};
        std::vector<CollisionBoxFeature> m_shape_box_feature{};

        // When a shape is not attached to a rigid body, cache world pose.
        std::vector<glm::vec3> m_shape_world_position{};
        std::vector<glm::quat> m_shape_world_rotation{};

        // Relative pose from rigid body center of mass frame.
        std::vector<glm::vec3> m_shape_local_to_center_position{};
        std::vector<glm::quat> m_shape_local_to_center_rotation{};
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED
