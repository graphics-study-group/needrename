#include "ConvexCollisionDetector.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeHelpers.h>
#include <Rhi/Pipeline/ComputeResourceBinding.h>
#include <Rhi/Pipeline/ComputeStage.h>
#include <Rhi/Pipeline/ShaderResourceBinding.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
    /**
     * @brief Load a precompiled physics SPIR-V blob from disk.
     */
    std::vector<uint32_t> LoadPhysicsSpirv(const char *relative_path) {
        std::filesystem::path full = std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR) / relative_path;
        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open physics SPIR-V: " + full.string());
        }
        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0u || size % sizeof(uint32_t) != 0u) {
            throw std::runtime_error("Invalid physics SPIR-V size: " + full.string());
        }
        std::vector<uint32_t> words(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
        return words;
    }

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

        std::unique_ptr<Rhi::ComputeStage> clear_stage{};
        Rhi::ComputeResourceBinding *clear_binding = nullptr;

        std::unique_ptr<Rhi::ComputeStage> detect_stage{};
        Rhi::ComputeResourceBinding *detect_binding = nullptr;

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

        void EnsureBuffers() {
            const auto &allocator = device_context.GetAllocatorState();
            const size_t result_entries = std::max<uint32_t>(1u, max_output_collision_pairs);

            {
                const size_t byte_size = result_entries * sizeof(glm::uvec2);
                if (!gpu_collision_ids || gpu_collision_ids->GetSize() != byte_size) {
                    gpu_collision_ids = Rhi::ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "CollisionIds"
                    );
                }
            }
            {
                const size_t byte_size = result_entries * sizeof(glm::vec4);
                if (!gpu_collision_normals || gpu_collision_normals->GetSize() != byte_size) {
                    gpu_collision_normals = Rhi::ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "CollisionNormals"
                    );
                }
            }
            {
                const size_t byte_size = result_entries * sizeof(glm::vec4);
                if (!gpu_contact_point_a || gpu_contact_point_a->GetSize() != byte_size) {
                    gpu_contact_point_a = Rhi::ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "ContactPointA"
                    );
                }
            }
            {
                const size_t byte_size = result_entries * sizeof(glm::vec4);
                if (!gpu_contact_point_b || gpu_contact_point_b->GetSize() != byte_size) {
                    gpu_contact_point_b = Rhi::ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "ContactPointB"
                    );
                }
            }
            {
                const size_t byte_size = sizeof(uint32_t);
                if (!gpu_collision_count || gpu_collision_count->GetSize() != byte_size) {
                    gpu_collision_count = Rhi::ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "CollisionCount"
                    );
                }
            }
        }

        void EnsureShadersAndBindings() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            {
                auto spirv = LoadPhysicsSpirv("solver/XPBDSolver/clear_int_buffer.comp.spv");
                clear_stage = std::make_unique<Rhi::ComputeStage>(device_context);
                clear_stage->Instantiate(spirv, "ConvexDetect ClearCount");
                clear_binding = &clear_stage->AllocateResourceBinding();
                auto &srb = clear_binding->GetShaderResourceBinding();
                srb.BindBuffer("Target", *gpu_collision_count);
            }

            {
                auto spirv = LoadPhysicsSpirv("collision/ConvexCollisionDetector/detect_collisions.comp.spv");
                detect_stage = std::make_unique<Rhi::ComputeStage>(device_context);
                detect_stage->Instantiate(spirv, "Convex Collision Detection");
                detect_binding = &detect_stage->AllocateResourceBinding();
                auto &srb = detect_binding->GetShaderResourceBinding();
                srb.BindBuffer("ShapeAlive", *cached_scene->GetGpuBuffers().shape_alive);
                srb.BindBuffer("ShapeType", *cached_scene->GetGpuBuffers().shape_type);
                srb.BindBuffer("ShapeFeature", *cached_scene->GetGpuBuffers().shape_feature);
                srb.BindBuffer("ShapeWorldPosition", *cached_scene->GetGpuBuffers().shape_world_position);
                srb.BindBuffer("ShapeWorldRotation", *cached_scene->GetGpuBuffers().shape_world_rotation);
                srb.BindBuffer("CollisionPairs", *cached_pair_buffer);
                srb.BindBuffer("PairCount", *cached_pair_count_buffer);
                srb.BindBuffer("CollisionIds", *gpu_collision_ids);
                srb.BindBuffer("CollisionNormals", *gpu_collision_normals);
                srb.BindBuffer("ContactPointA", *gpu_contact_point_a);
                srb.BindBuffer("ContactPointB", *gpu_contact_point_b);
                srb.BindBuffer("CollisionCount", *gpu_collision_count);
            }
        }

        void RebindDetectBuffers() {
            if (!detect_binding) return;
            auto &srb = detect_binding->GetShaderResourceBinding();
            srb.BindBuffer("ShapeAlive", *cached_scene->GetGpuBuffers().shape_alive);
            srb.BindBuffer("ShapeType", *cached_scene->GetGpuBuffers().shape_type);
            srb.BindBuffer("ShapeFeature", *cached_scene->GetGpuBuffers().shape_feature);
            srb.BindBuffer("ShapeWorldPosition", *cached_scene->GetGpuBuffers().shape_world_position);
            srb.BindBuffer("ShapeWorldRotation", *cached_scene->GetGpuBuffers().shape_world_rotation);
            srb.BindBuffer("CollisionPairs", *cached_pair_buffer);
            srb.BindBuffer("PairCount", *cached_pair_count_buffer);
            srb.BindBuffer("CollisionIds", *gpu_collision_ids);
            srb.BindBuffer("CollisionNormals", *gpu_collision_normals);
            srb.BindBuffer("ContactPointA", *gpu_contact_point_a);
            srb.BindBuffer("ContactPointB", *gpu_contact_point_b);
            srb.BindBuffer("CollisionCount", *gpu_collision_count);
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

        m_impl->EnsureShadersAndBindings();
        m_impl->RebindDetectBuffers();

        Rhi::PushConstants(cb, *m_impl->clear_stage, 1u);
        Rhi::BindComputeStage(cb, *m_impl->clear_stage);
        Rhi::BindComputeResource(cb, *m_impl->clear_stage, *m_impl->clear_binding);
        Rhi::DispatchCompute(cb, 1, 1, 1);

        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        struct DetectPushParams {
            float contact_margin;
            uint32_t shape_slot_count;
        };
        static_assert(sizeof(DetectPushParams) == 8, "DetectPushParams must match shader push block");
        const DetectPushParams params{m_impl->contact_margin, m_impl->shape_slot_count};

        uint32_t detect_wg = std::max(1u, (m_impl->max_input_collision_pairs + 63u) / 64u);
        Rhi::PushConstants(cb, *m_impl->detect_stage, params);
        Rhi::BindComputeStage(cb, *m_impl->detect_stage);
        Rhi::BindComputeResource(cb, *m_impl->detect_stage, *m_impl->detect_binding);
        Rhi::DispatchCompute(cb, detect_wg, 1, 1);
    }
} // namespace Engine
