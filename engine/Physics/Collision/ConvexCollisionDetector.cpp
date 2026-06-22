#include "ConvexCollisionDetector.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
#include <Render/Memory/MemoryAccessTypes.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
#include <Render/RenderSystem.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
    /**
     * @brief Load a precompiled physics SPIR-V blob from disk.
     *
     * Resolves @p relative_path against ENGINE_PHYSICS_SPIRV_DIR and reads the
     * file as 32-bit words.  Throws std::runtime_error (with the absolute path)
     * when the file is missing, empty, or its size is not a multiple of 4 bytes.
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
} // namespace

namespace Engine {

    struct ConvexCollisionDetector::Impl {
        RenderSystem &render_system;
        uint32_t max_collision_pairs;

        bool initialized = false;

        // ---- Collision-detection pipeline ----
        std::unique_ptr<ComputeStage> detect_stage{};
        ComputeResourceBinding *detect_resource_binding = nullptr;
        std::vector<uint32_t> detect_cached_spirv{};

        // ---- Owned GPU buffers ----

        // shape_slot_count: single-uint buffer read by detection shader.
        std::unique_ptr<ComputeBuffer> gpu_shape_slot_count{};

        // Collision result buffers.
        std::unique_ptr<ComputeBuffer> gpu_collision_ids{};
        std::unique_ptr<ComputeBuffer> gpu_collision_normals{};
        std::unique_ptr<ComputeBuffer> gpu_contact_point_a{};
        std::unique_ptr<ComputeBuffer> gpu_contact_point_b{};
        std::unique_ptr<ComputeBuffer> gpu_collision_count{};

        // Detector config uniform buffer (binding 12): single float contact_margin.
        std::unique_ptr<ComputeBuffer> gpu_detector_config{};

        float contact_margin = 0.001f;

        explicit Impl(RenderSystem &rs, uint32_t max_pairs, float margin) :
            render_system(rs), max_collision_pairs(max_pairs), contact_margin(margin) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /**
         * @brief Create or recreate all owned GPU buffers.
         */
        void EnsureBuffers() {
            const auto &allocator = render_system.GetAllocatorState();
            const size_t safe_pairs = std::max<uint32_t>(1u, max_collision_pairs);

            // Single-uint buffer for shape slot count.  Must be host-visible
            // so we can write the count each frame via GetVMAddress().
            {
                const size_t byte_size = sizeof(uint32_t);
                if (!gpu_shape_slot_count || gpu_shape_slot_count->GetSize() != byte_size) {
                    gpu_shape_slot_count =
                        ComputeBuffer::CreateUnique(allocator, byte_size, true, false, false, false, "ShapeSlotCount");
                }
            }

            // Result buffers — each manifold point is a separate entry (max 5 per pair:
            // 4 perturbation + optionally 1 MPR fallback).
            {
                const size_t byte_size = safe_pairs * sizeof(glm::uvec2);
                if (!gpu_collision_ids || gpu_collision_ids->GetSize() != byte_size) {
                    gpu_collision_ids =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "CollisionIds");
                }
            }
            {
                const size_t byte_size = safe_pairs * sizeof(glm::vec4);
                if (!gpu_collision_normals || gpu_collision_normals->GetSize() != byte_size) {
                    gpu_collision_normals = ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "CollisionNormals"
                    );
                }
            }
            {
                const size_t byte_size = safe_pairs * sizeof(glm::vec4);
                if (!gpu_contact_point_a || gpu_contact_point_a->GetSize() != byte_size) {
                    gpu_contact_point_a =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "ContactPointA");
                }
            }
            {
                const size_t byte_size = safe_pairs * sizeof(glm::vec4);
                if (!gpu_contact_point_b || gpu_contact_point_b->GetSize() != byte_size) {
                    gpu_contact_point_b =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "ContactPointB");
                }
            }
            {
                const size_t byte_size = sizeof(uint32_t);
                if (!gpu_collision_count || gpu_collision_count->GetSize() != byte_size) {
                    gpu_collision_count =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "CollisionCount");
                }
            }
            // Detector config uniform buffer: single float contact_margin.
            // Host-visible so we can update it each frame.
            {
                const size_t byte_size = sizeof(float);
                if (!gpu_detector_config || gpu_detector_config->GetSize() != byte_size) {
                    gpu_detector_config =
                        ComputeBuffer::CreateUnique(allocator, byte_size, true, false, false, false, "DetectorConfig");
                }
            }
        }

        /**
         * @brief Lazily load compute shaders and create pipelines.
         *
         * Idempotent — only does work on the first call.
         */
        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            // --- Collision-detection pipeline ---
            detect_cached_spirv = LoadPhysicsSpirv("solver/ConvexCollisionDetector/detect_collisions.comp.spv");
            detect_stage = std::make_unique<ComputeStage>(render_system);
            detect_stage->Instantiate(detect_cached_spirv, "Convex Collision Detection");
            detect_resource_binding = &detect_stage->AllocateResourceBinding();
        }

        /**
         * @brief Upload the shape slot count to the GPU buffer via host mapping.
         */
        void UpdateShapeSlotCount(uint32_t count) {
            auto *addr = reinterpret_cast<uint32_t *>(gpu_shape_slot_count->GetVMAddress());
            *addr = count;
        }

        void UpdateDetectorConfig() {
            auto *addr = reinterpret_cast<float *>(gpu_detector_config->GetVMAddress());
            *addr = contact_margin;
        }
    };

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    ConvexCollisionDetector::ConvexCollisionDetector(
        RenderSystem &render_system, uint32_t max_collision_pairs, float contact_margin
    ) : m_impl(std::make_unique<Impl>(render_system, max_collision_pairs, contact_margin)) {
    }

    ConvexCollisionDetector::~ConvexCollisionDetector() = default;

    bool ConvexCollisionDetector::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    CollisionResultBuffers ConvexCollisionDetector::GetCollisionResultBuffers() const noexcept {
        CollisionResultBuffers result;
        result.collision_ids = m_impl->gpu_collision_ids.get();
        result.collision_normals = m_impl->gpu_collision_normals.get();
        result.contact_point_a = m_impl->gpu_contact_point_a.get();
        result.contact_point_b = m_impl->gpu_contact_point_b.get();
        result.collision_count = m_impl->gpu_collision_count.get();
        result.max_collision_pairs = m_impl->max_collision_pairs;
        return result;
    }

    NarrowDetectorOutputHandles ConvexCollisionDetector::AddDetectPasses(
        RenderGraphBuilder &builder, PhysicsScene &physics_scene,
        const ComputeBuffer &pair_buffer, const ComputeBuffer &pair_count_buffer,
        const PhysicsSceneBufferHandles &handles,
        RGBufferHandle pair_buffer_handle, RGBufferHandle pair_count_handle
    ) {
        const auto gpu = physics_scene.GetGpuBuffers();

        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || gpu.shape_slot_count == 0u) {
            return {};
        }

        // --- Ensure buffers are created ---
        m_impl->EnsureBuffers();

        // --- Lazy initialization (first call only) ---
        m_impl->EnsureInitialized();

        // Upload shape slot count and detector config to GPU.
        m_impl->UpdateShapeSlotCount(gpu.shape_slot_count);
        m_impl->UpdateDetectorConfig();

        // ---- Bind resources for detection shader ----
        auto &detect_srb = m_impl->detect_resource_binding->GetShaderResourceBinding();
        detect_srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
        detect_srb.BindBuffer("ShapeType", *gpu.shape_type);
        detect_srb.BindBuffer("ShapeFeature", *gpu.shape_feature);
        detect_srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
        detect_srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);
        // External pair buffer.
        detect_srb.BindBuffer("CollisionPairs", pair_buffer);
        detect_srb.BindBuffer("PairCount", pair_count_buffer);
        // Result buffers.
        detect_srb.BindBuffer("CollisionIds", *m_impl->gpu_collision_ids);
        detect_srb.BindBuffer("CollisionNormals", *m_impl->gpu_collision_normals);
        detect_srb.BindBuffer("ContactPointA", *m_impl->gpu_contact_point_a);
        detect_srb.BindBuffer("ContactPointB", *m_impl->gpu_contact_point_b);
        detect_srb.BindBuffer("CollisionCount", *m_impl->gpu_collision_count);
        detect_srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);
        detect_srb.BindBuffer("DetectorConfig", *m_impl->gpu_detector_config);

        // ---- Use pre-imported scene buffer handles (no self-import) ----
        auto shape_alive_handle = handles.shape_alive;
        auto shape_type_handle = handles.shape_type;
        auto shape_feature_handle = handles.shape_feature;
        auto shape_world_pos_handle = handles.shape_world_position;
        auto shape_world_rot_handle = handles.shape_world_rotation;

        // ---- Owned result buffers (still imported here) ----
        auto slot_count_handle =
            builder.ImportExternalResource(*m_impl->gpu_shape_slot_count, {MemoryAccessTypeBufferBits::None});
        auto collision_ids_handle =
            builder.ImportExternalResource(*m_impl->gpu_collision_ids, {MemoryAccessTypeBufferBits::None});
        auto collision_normals_handle =
            builder.ImportExternalResource(*m_impl->gpu_collision_normals, {MemoryAccessTypeBufferBits::None});
        auto contact_a_handle =
            builder.ImportExternalResource(*m_impl->gpu_contact_point_a, {MemoryAccessTypeBufferBits::None});
        auto contact_b_handle =
            builder.ImportExternalResource(*m_impl->gpu_contact_point_b, {MemoryAccessTypeBufferBits::None});
        auto collision_count_handle =
            builder.ImportExternalResource(*m_impl->gpu_collision_count, {MemoryAccessTypeBufferBits::None});
        auto detector_config_handle =
            builder.ImportExternalResource(*m_impl->gpu_detector_config, {MemoryAccessTypeBufferBits::None});

        auto *detect_stage = m_impl->detect_stage.get();
        auto *detect_binding = m_impl->detect_resource_binding;

        // Dispatch with max_pair_capacity workgroups; extra threads early-return.
        const uint32_t detect_workgroups =
            std::max(1u, (m_impl->max_collision_pairs + 63u) / 64u);

        // ---- Pass: Collision detection ----
        builder.AddPass(
            RenderGraphPassBuilder{m_impl->render_system}
                .SetName("Convex Collision Detection")
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .UseBuffer(shape_alive_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_type_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_feature_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_world_pos_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_world_rot_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(pair_buffer_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(pair_count_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(collision_ids_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(collision_normals_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(contact_a_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(contact_b_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(
                    collision_count_handle,
                    {MemoryAccessTypeBufferBits::ShaderRandomRead, MemoryAccessTypeBufferBits::ShaderRandomWrite}
                )
                .UseBuffer(slot_count_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(detector_config_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .SetPassFunction(
                    [detect_stage, detect_binding, detect_workgroups, &physics_scene](
                        CommandBuffer &cb, const RenderGraph &
                    ) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*detect_stage);
                        cb.BindComputeResource(*detect_binding);
                        cb.DispatchCompute(detect_workgroups, 1, 1);
                    }
                )
                .Get()
        );

        return {
            .collision_ids = collision_ids_handle,
            .collision_normals = collision_normals_handle,
            .contact_point_a = contact_a_handle,
            .contact_point_b = contact_b_handle,
            .collision_count = collision_count_handle
        };
    }
} // namespace Engine
