#include "ConvexCollisionDetector.h"

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSpirvLoader.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>

#include <vector>

namespace {
    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };
} // namespace

namespace Engine {

    struct ConvexCollisionDetector::Impl {
        Rhi::DeviceContext &device_context;

        PhysicsScene *cached_scene = nullptr;
        const Rhi::ComputeBuffer *cached_pair_buffer = nullptr;
        const Rhi::ComputeBuffer *cached_pair_count_buffer = nullptr;

        uint32_t max_input_collision_pairs = 1;
        uint32_t max_output_collision_pairs = 1;
        float contact_margin = 0.001f;
        uint32_t shape_slot_count = 0;

        bool shaders_loaded = false;

        Rhi::ComputeKernel *clear_kernel = nullptr;
        Rhi::ComputeKernel *detect_kernel = nullptr;

        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_ids{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_normals{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_point_a{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_point_b{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_count{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /// @brief Exact-size resize that keeps the buffer object at the same address.
        void EnsureBuffer(std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name) {
            const auto &allocator = device_context.GetAllocatorState();
            if (!buf) {
                buf = Rhi::ComputeBuffer::CreateUnique(allocator, bytes, false, false, false, false, name);
            } else if (buf->GetSize() != bytes) {
                buf->Reallocate(allocator, bytes);
            }
        }

        void EnsureBuffers() {
            const size_t result_entries = std::max<uint32_t>(1u, max_output_collision_pairs);

            EnsureBuffer(gpu_collision_ids, result_entries * sizeof(glm::uvec2), "CollisionIds");
            EnsureBuffer(gpu_collision_normals, result_entries * sizeof(glm::vec4), "CollisionNormals");
            EnsureBuffer(gpu_contact_point_a, result_entries * sizeof(glm::vec4), "ContactPointA");
            EnsureBuffer(gpu_contact_point_b, result_entries * sizeof(glm::vec4), "ContactPointB");
            EnsureBuffer(gpu_collision_count, sizeof(uint32_t), "CollisionCount");
        }

        void EnsureShadersAndBindings() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            // `clear_int_buffer.comp` is shared with the XPBD solver: both
            // components resolve to the same device-level kernel.
            clear_kernel = &LoadPhysicsKernel(
                device_context, "solver/XPBDSolver/clear_int_buffer.comp.spv", "ConvexDetect ClearCount"
            );
            detect_kernel = &LoadPhysicsKernel(
                device_context,
                "collision/ConvexCollisionDetector/detect_collisions.comp.spv",
                "Convex Collision Detection"
            );
        }
    };

    ConvexCollisionDetector::ConvexCollisionDetector(Rhi::DeviceContext &device_context) :
        m_impl(std::make_unique<Impl>(device_context)) {
    }

    ConvexCollisionDetector::~ConvexCollisionDetector() = default;

    bool ConvexCollisionDetector::IsInitialized() const noexcept {
        return m_impl->shaders_loaded;
    }

    void ConvexCollisionDetector::Configure(
        PhysicsScene &scene,
        uint32_t max_input_collision_pairs,
        uint32_t max_output_collision_pairs,
        float contact_margin,
        const Rhi::ComputeBuffer &pair_buffer,
        const Rhi::ComputeBuffer &pair_count_buffer
    ) {
        m_impl->cached_scene = &scene;
        m_impl->cached_pair_buffer = &pair_buffer;
        m_impl->cached_pair_count_buffer = &pair_count_buffer;
        m_impl->max_input_collision_pairs = std::max(1u, max_input_collision_pairs);
        m_impl->max_output_collision_pairs = std::max(1u, max_output_collision_pairs);
        m_impl->contact_margin = contact_margin;

        m_impl->EnsureBuffers();
        m_impl->EnsureShadersAndBindings();

        const auto gpu = scene.GetGpuBuffers();
        m_impl->shape_slot_count = gpu.shape_slot_count;
    }

    CollisionResultBuffers ConvexCollisionDetector::GetResultBuffers() const noexcept {
        CollisionResultBuffers result;
        result.collision_ids = m_impl->gpu_collision_ids.get();
        result.collision_normals = m_impl->gpu_collision_normals.get();
        result.contact_point_a = m_impl->gpu_contact_point_a.get();
        result.contact_point_b = m_impl->gpu_contact_point_b.get();
        result.collision_count = m_impl->gpu_collision_count.get();
        result.max_output_collision_pairs = m_impl->max_output_collision_pairs;
        return result;
    }

    void ConvexCollisionDetector::Record(vk::CommandBuffer cb) {
        assert(m_impl->cached_scene && "Configure must be called before Record");
        const auto gpu = m_impl->cached_scene->GetGpuBuffers();

        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || gpu.shape_slot_count == 0u) {
            return;
        }

        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        // Every kernel was acquired in Configure: recording creates no pipeline.
        m_impl->clear_kernel->Dispatch(
            cb, {{"Target", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_collision_count)}}, 1, 1, 1, 1u
        );

        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        struct DetectPushParams {
            float contact_margin;
            uint32_t shape_slot_count;
        };
        static_assert(sizeof(DetectPushParams) == 8, "DetectPushParams must match shader push block");
        const DetectPushParams params{m_impl->contact_margin, m_impl->shape_slot_count};

        uint32_t detect_wg = std::max(1u, (m_impl->max_input_collision_pairs + 63u) / 64u);
        const auto scene_gpu = m_impl->cached_scene->GetGpuBuffers();
        m_impl->detect_kernel->Dispatch(
            cb,
            {{"ShapeAlive", Rhi::ComputeKernelResource::Buffer(*scene_gpu.shape_alive)},
             {"ShapeType", Rhi::ComputeKernelResource::Buffer(*scene_gpu.shape_type)},
             {"ShapeFeature", Rhi::ComputeKernelResource::Buffer(*scene_gpu.shape_feature)},
             {"ShapeWorldPosition", Rhi::ComputeKernelResource::Buffer(*scene_gpu.shape_world_position)},
             {"ShapeWorldRotation", Rhi::ComputeKernelResource::Buffer(*scene_gpu.shape_world_rotation)},
             {"CollisionPairs", Rhi::ComputeKernelResource::Buffer(*m_impl->cached_pair_buffer)},
             {"PairCount", Rhi::ComputeKernelResource::Buffer(*m_impl->cached_pair_count_buffer)},
             {"CollisionIds", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_collision_ids)},
             {"CollisionNormals", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_collision_normals)},
             {"ContactPointA", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_point_a)},
             {"ContactPointB", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_point_b)},
             {"CollisionCount", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_collision_count)}},
            detect_wg,
            1,
            1,
            params
        );
    }
} // namespace Engine
