#ifndef ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED
#define ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED

#include <AnnoRefl/macros.h>
#include <AnnoRefl/serialization_glm.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace Engine {
    namespace Rhi {
        class ComputeBuffer;
    }
    class RenderSystem;

    enum class REFL_SER_CLASS() CollisionShapeType {
        Box = 0,
        Sphere = 1,
        Cylinder = 2
    };

    struct GpuFixedJoint {
        uint32_t obj1_index;
        uint32_t obj2_index;
        float compliance;
        float _pad;
        glm::vec4 initial_rel_pos_local;
        glm::vec4 initial_rel_rotation;
    };

    struct GpuHingeJoint {
        uint32_t obj1_index;
        uint32_t obj2_index;
        float compliance;
        float _pad;
        glm::vec4 hinge_axis_obj1;
        glm::vec4 hinge_anchor_obj1;
        glm::vec4 initial_rel_pos_local;
        glm::vec4 initial_rel_rotation;
    };

    static_assert(sizeof(GpuFixedJoint) == 48, "GpuFixedJoint must be 48 bytes (std430)");
    static_assert(sizeof(GpuHingeJoint) == 80, "GpuHingeJoint must be 80 bytes (std430)");

    struct RigidBodyComDescriptor;
    struct CollisionShapeComDescriptor;

    class PhysicsScene {
    public:
        static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;
        static constexpr uint32_t MAX_FILTER_ENTRIES = 8;

        explicit PhysicsScene(uint32_t scene_id);
        ~PhysicsScene();

        PhysicsScene(const PhysicsScene &) = delete;
        PhysicsScene &operator=(const PhysicsScene &) = delete;
        PhysicsScene(PhysicsScene &&) = delete;
        PhysicsScene &operator=(PhysicsScene &&) = delete;

        uint32_t GetSceneID() const noexcept;
        void Clear();

        uint32_t AllocateRigidBodySlot();
        uint32_t AllocateCollisionShapeSlot();
        uint32_t AllocateFixedJoint();
        uint32_t AllocateHingeJoint();

        void UnregisterRigidBody(uint32_t rigid_body_index);
        void UnregisterCollisionShape(uint32_t shape_index);
        void UnregisterFixedJoint(uint32_t joint_index);
        void UnregisterHingeJoint(uint32_t joint_index);

        void SubmitRigidBody(uint32_t rigid_body_index, const RigidBodyComDescriptor &desc);
        void SubmitCollisionShape(uint32_t shape_index, const CollisionShapeComDescriptor &desc);
        void SetShapeFilters(const std::vector<uint32_t> &filter_data, uint32_t shape_count);
        void SubmitFixedJoint(uint32_t joint_idx, const GpuFixedJoint &joint);
        void SubmitHingeJoint(uint32_t joint_idx, const GpuHingeJoint &joint);

        void SyncGpuBuffers(RenderSystem &render_system);

        struct PhysicsGpuBuffers {
            const Rhi::ComputeBuffer *rigid_body_alive{};
            const Rhi::ComputeBuffer *rigid_body_mass{};
            const Rhi::ComputeBuffer *rigid_body_static_friction{};
            const Rhi::ComputeBuffer *rigid_body_dynamic_friction{};
            const Rhi::ComputeBuffer *rigid_body_restitution{};
            const Rhi::ComputeBuffer *rigid_body_is_kinematic{};
            const Rhi::ComputeBuffer *rigid_body_center_world_position{};
            const Rhi::ComputeBuffer *rigid_body_center_world_rotation{};
            const Rhi::ComputeBuffer *rigid_body_inertia{};
            const Rhi::ComputeBuffer *rigid_body_inverse_inertia{};
            const Rhi::ComputeBuffer *rigid_body_linear_velocity{};
            const Rhi::ComputeBuffer *rigid_body_angular_velocity{};
            const Rhi::ComputeBuffer *rigid_body_external_force{};
            const Rhi::ComputeBuffer *rigid_body_external_torque{};

            const Rhi::ComputeBuffer *shape_alive{};
            const Rhi::ComputeBuffer *shape_type{};
            const Rhi::ComputeBuffer *shape_bound_rigid_body{};
            const Rhi::ComputeBuffer *shape_feature{};
            const Rhi::ComputeBuffer *shape_local_position{};
            const Rhi::ComputeBuffer *shape_local_rotation{};
            const Rhi::ComputeBuffer *shape_world_position{};
            const Rhi::ComputeBuffer *shape_world_rotation{};

            const Rhi::ComputeBuffer *model_matrices{};

            const Rhi::ComputeBuffer *shape_filter_data{};

            const Rhi::ComputeBuffer *gpu_fixed_joints{};
            const Rhi::ComputeBuffer *gpu_fixed_joint_alive{};
            const Rhi::ComputeBuffer *gpu_hinge_joints{};
            const Rhi::ComputeBuffer *gpu_hinge_joint_alive{};

            uint32_t rigid_body_slot_count{0};
            uint32_t shape_slot_count{0};
            uint32_t fixed_joint_count{0};
            uint32_t hinge_joint_count{0};
        };

        PhysicsGpuBuffers GetGpuBuffers() const noexcept;

        bool IsRigidBodyIndexValid(uint32_t rigid_body_index) const;
        bool IsShapeIndexValid(uint32_t shape_index) const;

        void DebugPrint() const;

        void SetSimulationEnabled(bool enabled);
        bool IsSimulationEnabled() const noexcept;

    private:
        void RefreshGpuBuffers(RenderSystem &render_system);

        uint32_t m_scene_id{0};

        bool m_simulation_enabled = false;

        std::vector<uint32_t> m_rigid_body_alive{};
        std::vector<float> m_rigid_body_mass{};
        std::vector<float> m_rigid_body_static_friction{};
        std::vector<float> m_rigid_body_dynamic_friction{};
        std::vector<float> m_rigid_body_restitution{};
        std::vector<uint32_t> m_rigid_body_is_kinematic{};
        std::vector<glm::vec4> m_rigid_body_center_world_position{};
        std::vector<glm::vec4> m_rigid_body_center_world_rotation{};
        std::vector<glm::mat4> m_rigid_body_inertia{};
        std::vector<glm::mat4> m_rigid_body_inverse_inertia{};
        std::vector<glm::vec4> m_rigid_body_linear_velocity{};
        std::vector<glm::vec4> m_rigid_body_angular_velocity{};
        std::vector<glm::vec4> m_rigid_body_external_force{};
        std::vector<glm::vec4> m_rigid_body_external_torque{};

        std::vector<uint32_t> m_shape_alive{};
        std::vector<uint32_t> m_shape_to_rigid_body{};
        std::vector<uint32_t> m_shape_type{};
        std::vector<glm::vec4> m_shape_feature{};
        std::vector<glm::vec4> m_shape_local_position{};
        std::vector<glm::vec4> m_shape_local_rotation{};
        std::vector<glm::vec4> m_shape_world_position{};
        std::vector<glm::vec4> m_shape_world_rotation{};

        uint32_t m_gpu_rigid_body_slot_count{0};
        uint32_t m_gpu_shape_slot_count{0};

        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_alive{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_mass{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_static_friction{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_dynamic_friction{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_restitution{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_is_kinematic{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_center_world_position{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_center_world_rotation{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_inertia{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_inverse_inertia{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_linear_velocity{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_angular_velocity{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_external_force{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_rigid_body_external_torque{};

        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_alive{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_type{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_bound_rigid_body{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_feature{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_local_position{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_local_rotation{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_world_position{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_world_rotation{};

        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_model_matrices{};

        std::vector<GpuFixedJoint> m_fixed_joints{};
        std::vector<uint32_t> m_fixed_joint_alive{};
        std::vector<GpuHingeJoint> m_hinge_joints{};
        std::vector<uint32_t> m_hinge_joint_alive{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_fixed_joints{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_fixed_joint_alive{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_hinge_joints{};
        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_hinge_joint_alive{};

        std::vector<uint32_t> m_shape_filter_data{};

        std::unique_ptr<Rhi::ComputeBuffer> m_gpu_shape_filter_data{};
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSCENE_INCLUDED