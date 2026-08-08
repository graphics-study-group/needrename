#include "ConvexCollisionDetector.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Rhi/ComputeBuffer.h>
#include <Rhi/ComputeHelpers.h>
#include <Rhi/ComputeResourceBinding.h>
#include <Rhi/ComputeStage.h>
#include <Rhi/DeviceBuffer.h>
#include <Rhi/DeviceContext.h>
#include <Rhi/ShaderResourceBinding.h>

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

        bool shaders_loaded = false;

        std::unique_ptr<Rhi::ComputeStage> clear_stage{};
        Rhi::ComputeResourceBinding *clear_binding = nullptr;

        std::unique_ptr<Rhi::ComputeStage> detect_stage{};
        Rhi::ComputeResourceBinding *detect_binding = nullptr;

        std::unique_ptr<Rhi::ComputeBuffer> gpu_shape_slot_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_ids{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_normals{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_point_a{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_point_b{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_detector_config{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_one{};

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
                const size_t byte_size = sizeof(uint32_t);
                if (!gpu_shape_slot_count || gpu_shape_slot_count->GetSize() != byte_size) {
                    gpu_shape_slot_count = Rhi::ComputeBuffer::CreateUnique(
                        allocator, byte_size, true, false, false, false, "ShapeSlotCount"
                    );
                }
            }
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
            {
                const size_t byte_size = sizeof(float);
                if (!gpu_detector_config || gpu_detector_config->GetSize() != byte_size) {
                    gpu_detector_config = Rhi::ComputeBuffer::CreateUnique(
                        allocator, byte_size, true, false, false, false, "DetectorConfig"
                    );
                }
            }
            {
                if (!gpu_one || gpu_one->GetSize() < sizeof(uint32_t)) {
                    gpu_one = Rhi::ComputeBuffer::CreateUnique(
                        allocator, sizeof(uint32_t), true, false, false, false, "NarrowOne"
                    );
                    auto *addr = reinterpret_cast<uint32_t *>(gpu_one->GetVMAddress());
                    *addr = 1u;
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
                clear_binding = &clear_stage->AllocateResourceBinding(3);
                auto &srb = clear_binding->GetShaderResourceBinding();
                srb.BindBuffer("Target", *gpu_collision_count);
                srb.BindBuffer("ElemCount", *gpu_one);
            }

            {
                auto spirv = LoadPhysicsSpirv("collision/ConvexCollisionDetector/detect_collisions.comp.spv");
                detect_stage = std::make_unique<Rhi::ComputeStage>(device_context);
                detect_stage->Instantiate(spirv, "Convex Collision Detection");
                detect_binding = &detect_stage->AllocateResourceBinding(3);
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
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("DetectorConfig", *gpu_detector_config);
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
            srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
            srb.BindBuffer("DetectorConfig", *gpu_detector_config);
        }

        void UpdateShapeSlotCount(uint32_t count) {
            auto *addr = reinterpret_cast<uint32_t *>(gpu_shape_slot_count->GetVMAddress());
            *addr = count;
        }

        void UpdateDetectorConfig() {
            auto *addr = reinterpret_cast<float *>(gpu_detector_config->GetVMAddress());
            *addr = contact_margin;
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
        if (gpu.shape_slot_count > 0u) {
            m_impl->UpdateShapeSlotCount(gpu.shape_slot_count);
        }
        m_impl->UpdateDetectorConfig();
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

        const uint32_t frame = m_frame_counter++ % 3;
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->EnsureShadersAndBindings();
        m_impl->RebindDetectBuffers();

        Rhi::BindComputeStage(cb, *m_impl->clear_stage);
        Rhi::BindComputeResource(cb, *m_impl->clear_stage, *m_impl->clear_binding, frame);
        Rhi::DispatchCompute(cb, 1, 1, 1);

        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        uint32_t detect_wg = std::max(1u, (m_impl->max_input_collision_pairs + 63u) / 64u);
        Rhi::BindComputeStage(cb, *m_impl->detect_stage);
        Rhi::BindComputeResource(cb, *m_impl->detect_stage, *m_impl->detect_binding, frame);
        Rhi::DispatchCompute(cb, detect_wg, 1, 1);
    }
} // namespace Engine
