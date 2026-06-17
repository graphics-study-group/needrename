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

        // ---- Pair-generation pipeline ----
        std::unique_ptr<ComputeStage> pair_gen_stage{};
        ComputeResourceBinding *pair_gen_resource_binding = nullptr;
        std::vector<uint32_t> pair_gen_cached_spirv{};

        // ---- Collision-detection pipeline ----
        std::unique_ptr<ComputeStage> detect_stage{};
        ComputeResourceBinding *detect_resource_binding = nullptr;
        std::vector<uint32_t> detect_cached_spirv{};

        // ---- Owned GPU buffers ----

        // shape_slot_count: single-uint buffer read by both shaders.
        std::unique_ptr<ComputeBuffer> gpu_shape_slot_count{};

        // Collision pair input buffer (written by generate_pairs, read by detect_collisions).
        std::unique_ptr<ComputeBuffer> gpu_collision_pairs{};

        // Collision result buffers.
        std::unique_ptr<ComputeBuffer> gpu_collision_ids{};
        std::unique_ptr<ComputeBuffer> gpu_collision_normals{};
        std::unique_ptr<ComputeBuffer> gpu_contact_point_a{};
        std::unique_ptr<ComputeBuffer> gpu_contact_point_b{};
        std::unique_ptr<ComputeBuffer> gpu_collision_count{};

        explicit Impl(RenderSystem &rs, uint32_t max_pairs) : render_system(rs), max_collision_pairs(max_pairs) {
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

            // Pair buffer.
            {
                const size_t byte_size = safe_pairs * sizeof(glm::uvec2);
                if (!gpu_collision_pairs || gpu_collision_pairs->GetSize() != byte_size) {
                    gpu_collision_pairs = ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "CollisionPairInput"
                    );
                }
            }

            // Result buffers — each manifold point is a separate entry,
            // so all buffers are sized max_pairs * 4 (max 4 points per pair).
            const size_t safe_slots = safe_pairs * 4u;

            {
                const size_t byte_size = safe_slots * sizeof(glm::uvec2);
                if (!gpu_collision_ids || gpu_collision_ids->GetSize() != byte_size) {
                    gpu_collision_ids =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "CollisionIds");
                }
            }
            {
                const size_t byte_size = safe_slots * sizeof(glm::vec4);
                if (!gpu_collision_normals || gpu_collision_normals->GetSize() != byte_size) {
                    gpu_collision_normals = ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "CollisionNormals"
                    );
                }
            }
            {
                const size_t byte_size = safe_slots * sizeof(glm::vec4);
                if (!gpu_contact_point_a || gpu_contact_point_a->GetSize() != byte_size) {
                    gpu_contact_point_a =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "ContactPointA");
                }
            }
            {
                const size_t byte_size = safe_slots * sizeof(glm::vec4);
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
        }

        /**
         * @brief Lazily load compute shaders and create pipelines.
         *
         * Idempotent — only does work on the first call.
         */
        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            // --- Pair-generation pipeline ---
            pair_gen_cached_spirv = LoadPhysicsSpirv("solver/ConvexCollisionDetector/generate_pairs.comp.spv");
            pair_gen_stage = std::make_unique<ComputeStage>(render_system);
            pair_gen_stage->Instantiate(pair_gen_cached_spirv, "Collision Pair Generation");
            pair_gen_resource_binding = &pair_gen_stage->AllocateResourceBinding();

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
    };

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    ConvexCollisionDetector::ConvexCollisionDetector(RenderSystem &render_system, uint32_t max_collision_pairs) :
        m_impl(std::make_unique<Impl>(render_system, max_collision_pairs)) {
    }

    ConvexCollisionDetector::~ConvexCollisionDetector() = default;

    bool ConvexCollisionDetector::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t ConvexCollisionDetector::GetMaxCollisionPairs() const noexcept {
        return m_impl->max_collision_pairs;
    }

    const ComputeBuffer &ConvexCollisionDetector::GetCollisionIds() const noexcept {
        return *m_impl->gpu_collision_ids;
    }
    const ComputeBuffer &ConvexCollisionDetector::GetCollisionNormals() const noexcept {
        return *m_impl->gpu_collision_normals;
    }
    const ComputeBuffer &ConvexCollisionDetector::GetContactPointA() const noexcept {
        return *m_impl->gpu_contact_point_a;
    }
    const ComputeBuffer &ConvexCollisionDetector::GetContactPointB() const noexcept {
        return *m_impl->gpu_contact_point_b;
    }
    const ComputeBuffer &ConvexCollisionDetector::GetCollisionCount() const noexcept {
        return *m_impl->gpu_collision_count;
    }

    void ConvexCollisionDetector::Step(RenderGraphBuilder &builder, PhysicsScene &physics_scene) {
        const auto gpu = physics_scene.GetGpuBuffers();

        // Bail if essential shape buffers are not available or no shapes exist.
        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || gpu.shape_slot_count == 0u) {
            return;
        }

        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t total_pairs = (shape_count * (shape_count - 1u)) / 2u;

        // Nothing to detect with 0 or 1 shapes.
        if (total_pairs == 0u) {
            return;
        }

        // --- Ensure buffers are created ---
        m_impl->EnsureBuffers();

        // --- Lazy initialization (first call only) ---
        m_impl->EnsureInitialized();

        // Upload shape slot count to GPU.
        m_impl->UpdateShapeSlotCount(shape_count);

        // ---- Bind resources for pair-generation shader ----
        auto &pair_srb = m_impl->pair_gen_resource_binding->GetShaderResourceBinding();
        pair_srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);
        pair_srb.BindBuffer("CollisionPairs", *m_impl->gpu_collision_pairs);
        pair_srb.BindBuffer("CollisionCount", *m_impl->gpu_collision_count);

        // ---- Bind resources for detection shader ----
        // PhysicsScene shape buffers (readonly).
        auto &detect_srb = m_impl->detect_resource_binding->GetShaderResourceBinding();
        detect_srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
        detect_srb.BindBuffer("ShapeType", *gpu.shape_type);
        detect_srb.BindBuffer("ShapeHalfExtents", *gpu.shape_half_extents);
        detect_srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
        detect_srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);
        // Owned buffers.
        detect_srb.BindBuffer("CollisionPairs", *m_impl->gpu_collision_pairs);
        detect_srb.BindBuffer("CollisionIds", *m_impl->gpu_collision_ids);
        detect_srb.BindBuffer("CollisionNormals", *m_impl->gpu_collision_normals);
        detect_srb.BindBuffer("ContactPointA", *m_impl->gpu_contact_point_a);
        detect_srb.BindBuffer("ContactPointB", *m_impl->gpu_contact_point_b);
        detect_srb.BindBuffer("CollisionCount", *m_impl->gpu_collision_count);
        detect_srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);

        // ---- Import external resources into the render graph ----

        // PhysicsScene shape buffers.
        auto shape_alive_handle = builder.ImportExternalResource(*gpu.shape_alive, {MemoryAccessTypeBufferBits::None});
        auto shape_type_handle = builder.ImportExternalResource(*gpu.shape_type, {MemoryAccessTypeBufferBits::None});
        auto shape_half_extents_handle =
            builder.ImportExternalResource(*gpu.shape_half_extents, {MemoryAccessTypeBufferBits::None});
        auto shape_world_pos_handle =
            builder.ImportExternalResource(*gpu.shape_world_position, {MemoryAccessTypeBufferBits::None});
        auto shape_world_rot_handle =
            builder.ImportExternalResource(*gpu.shape_world_rotation, {MemoryAccessTypeBufferBits::None});

        // Owned buffers.
        auto slot_count_handle =
            builder.ImportExternalResource(*m_impl->gpu_shape_slot_count, {MemoryAccessTypeBufferBits::None});
        auto pairs_handle =
            builder.ImportExternalResource(*m_impl->gpu_collision_pairs, {MemoryAccessTypeBufferBits::None});
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

        // Capture pointers for the pass functions.  The detector owns the
        // ComputeStage instances, so these pointers outlive the lambdas.
        auto *pg_stage = m_impl->pair_gen_stage.get();
        auto *pg_binding = m_impl->pair_gen_resource_binding;
        auto *detect_stage = m_impl->detect_stage.get();
        auto *detect_binding = m_impl->detect_resource_binding;

        // Dispatch sizes.
        // Always dispatch at least 1 workgroup for pair generation so thread 0
        // resets the collision count.
        const uint32_t pair_gen_workgroups = std::max(1u, (total_pairs + 63u) / 64u);
        const uint32_t detect_workgroups = (total_pairs + 63u) / 64u;

        // ---- Pass 1: GPU pair generation ----
        builder.AddPass(
            RenderGraphPassBuilder{m_impl->render_system}
                .SetName("Collision Pair Generation")
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .UseBuffer(slot_count_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(pairs_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(collision_count_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .SetPassFunction(
                    [pg_stage, pg_binding, pair_gen_workgroups](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*pg_stage);
                        cb.BindComputeResource(*pg_binding);
                        cb.DispatchCompute(pair_gen_workgroups, 1, 1);
                    }
                )
                .Get()
        );

        // ---- Pass 2: Collision detection ----
        builder.AddPass(
            RenderGraphPassBuilder{m_impl->render_system}
                .SetName("Convex Collision Detection")
                .SetAffinity(RenderGraphPassAffinity::Compute)
                // PhysicsScene shape buffers (readonly).
                .UseBuffer(shape_alive_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_type_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_half_extents_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_world_pos_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(shape_world_rot_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                // Pair buffer (read).
                .UseBuffer(pairs_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                // Result buffers (write).
                .UseBuffer(collision_ids_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(collision_normals_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(contact_a_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(contact_b_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                .UseBuffer(
                    collision_count_handle,
                    {MemoryAccessTypeBufferBits::ShaderRandomRead, MemoryAccessTypeBufferBits::ShaderRandomWrite}
                )
                // Slot count (readonly).
                .UseBuffer(slot_count_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .SetPassFunction(
                    [detect_stage, detect_binding, detect_workgroups](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*detect_stage);
                        cb.BindComputeResource(*detect_binding);
                        cb.DispatchCompute(detect_workgroups, 1, 1);
                    }
                )
                .Get()
        );
    }
} // namespace Engine
