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
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
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

        // Cached references from Configure.
        PhysicsScene *cached_scene = nullptr;
        const ComputeBuffer *cached_pair_buffer = nullptr;
        const ComputeBuffer *cached_pair_count_buffer = nullptr;

        // Sizing / config cached from Configure.
        uint32_t max_input_collision_pairs = 1;
        uint32_t max_output_collision_pairs = 1;
        float contact_margin = 0.001f;
        uint32_t cached_max_pairs_for_rg = 0; // For RG rebuild detection.

        bool shaders_loaded = false;

        // ---- Clear pipeline (resets collision_count each frame) ----
        std::unique_ptr<ComputeStage> clear_stage{};
        ComputeResourceBinding *clear_resource_binding = nullptr;
        std::vector<uint32_t> clear_cached_spirv{};

        // ---- Collision-detection pipeline ----
        std::unique_ptr<ComputeStage> detect_stage{};
        ComputeResourceBinding *detect_resource_binding = nullptr;
        std::vector<uint32_t> detect_cached_spirv{};

        // ---- Owned GPU buffers ----
        std::unique_ptr<ComputeBuffer> gpu_shape_slot_count{};
        std::unique_ptr<ComputeBuffer> gpu_collision_ids{};
        std::unique_ptr<ComputeBuffer> gpu_collision_normals{};
        std::unique_ptr<ComputeBuffer> gpu_contact_point_a{};
        std::unique_ptr<ComputeBuffer> gpu_contact_point_b{};
        std::unique_ptr<ComputeBuffer> gpu_collision_count{};
        std::unique_ptr<ComputeBuffer> gpu_detector_config{};
        std::unique_ptr<ComputeBuffer> gpu_one{};

        // ---- Self-owned RenderGraph ----
        std::unique_ptr<RenderGraph> render_graph{};

        explicit Impl(RenderSystem &rs) : render_system(rs) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureBuffers() {
            const auto &allocator = render_system.GetAllocatorState();
            const size_t result_entries = std::max<uint32_t>(1u, max_output_collision_pairs);

            {
                const size_t byte_size = sizeof(uint32_t);
                if (!gpu_shape_slot_count || gpu_shape_slot_count->GetSize() != byte_size) {
                    gpu_shape_slot_count =
                        ComputeBuffer::CreateUnique(allocator, byte_size, true, false, false, false, "ShapeSlotCount");
                }
            }
            {
                const size_t byte_size = result_entries * sizeof(glm::uvec2);
                if (!gpu_collision_ids || gpu_collision_ids->GetSize() != byte_size) {
                    gpu_collision_ids =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "CollisionIds");
                }
            }
            {
                const size_t byte_size = result_entries * sizeof(glm::vec4);
                if (!gpu_collision_normals || gpu_collision_normals->GetSize() != byte_size) {
                    gpu_collision_normals = ComputeBuffer::CreateUnique(
                        allocator, byte_size, false, false, false, false, "CollisionNormals"
                    );
                }
            }
            {
                const size_t byte_size = result_entries * sizeof(glm::vec4);
                if (!gpu_contact_point_a || gpu_contact_point_a->GetSize() != byte_size) {
                    gpu_contact_point_a =
                        ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, "ContactPointA");
                }
            }
            {
                const size_t byte_size = result_entries * sizeof(glm::vec4);
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
            {
                const size_t byte_size = sizeof(float);
                if (!gpu_detector_config || gpu_detector_config->GetSize() != byte_size) {
                    gpu_detector_config =
                        ComputeBuffer::CreateUnique(allocator, byte_size, true, false, false, false, "DetectorConfig");
                }
            }
            {
                if (!gpu_one || gpu_one->GetSize() < sizeof(uint32_t)) {
                    gpu_one = ComputeBuffer::CreateUnique(
                        allocator, sizeof(uint32_t), true, false, false, false, "NarrowOne"
                    );
                    auto *addr = reinterpret_cast<uint32_t *>(gpu_one->GetVMAddress());
                    *addr = 1u;
                }
            }
        }

        void EnsureShadersLoaded() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            clear_cached_spirv = LoadPhysicsSpirv("solver/XPBDSolver/clear_int_buffer.comp.spv");
            clear_stage = std::make_unique<ComputeStage>(render_system);
            clear_stage->Instantiate(clear_cached_spirv, "ConvexDetect ClearCount");
            clear_resource_binding = &clear_stage->AllocateResourceBinding();

            detect_cached_spirv = LoadPhysicsSpirv("collision/ConvexCollisionDetector/detect_collisions.comp.spv");
            detect_stage = std::make_unique<ComputeStage>(render_system);
            detect_stage->Instantiate(detect_cached_spirv, "Convex Collision Detection");
            detect_resource_binding = &detect_stage->AllocateResourceBinding();
        }

        void UpdateShapeSlotCount(uint32_t count) {
            auto *addr = reinterpret_cast<uint32_t *>(gpu_shape_slot_count->GetVMAddress());
            *addr = count;
        }

        void UpdateDetectorConfig() {
            auto *addr = reinterpret_cast<float *>(gpu_detector_config->GetVMAddress());
            *addr = contact_margin;
        }

        std::unique_ptr<RenderGraph> BuildRenderGraph() {
            assert(cached_scene && "Configure must be called before Detect");
            const auto gpu = cached_scene->GetGpuBuffers();

            RenderGraphBuilder builder{render_system};

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};

            // prev_access = None for detector-owned buffers (first write/use in the detector).
            // For scene buffers, prev_access = RR — the broad-phase (which runs before us)
            // only reads these.  For the pair buffers (broad-phase output), prev_access = WW
            // since broad-phase wrote them.

            // --- Import scene buffers (read-only) ---
            auto shape_alive_h = builder.ImportExternalResource(*gpu.shape_alive, RR);
            auto shape_type_h = builder.ImportExternalResource(*gpu.shape_type, RR);
            auto shape_feature_h = builder.ImportExternalResource(*gpu.shape_feature, RR);
            auto shape_wpos_h = builder.ImportExternalResource(*gpu.shape_world_position, RR);
            auto shape_wrot_h = builder.ImportExternalResource(*gpu.shape_world_rotation, RR);

            // --- Import pair buffers (broad-phase output, written by broad-phase) ---
            auto pair_h = builder.ImportExternalResource(*cached_pair_buffer, WW);
            auto pair_cnt_h = builder.ImportExternalResource(*cached_pair_count_buffer, WW);

            // --- Import owned buffers ---
            auto slot_cnt_h = builder.ImportExternalResource(*gpu_shape_slot_count, {AT::None});
            auto ids_h = builder.ImportExternalResource(*gpu_collision_ids, {AT::None});
            auto normals_h = builder.ImportExternalResource(*gpu_collision_normals, {AT::None});
            auto cpa_h = builder.ImportExternalResource(*gpu_contact_point_a, {AT::None});
            auto cpb_h = builder.ImportExternalResource(*gpu_contact_point_b, {AT::None});
            auto count_h = builder.ImportExternalResource(*gpu_collision_count, {AT::None});
            auto cfg_h = builder.ImportExternalResource(*gpu_detector_config, {AT::None});
            auto one_h = builder.ImportExternalResource(*gpu_one, {AT::None});

            // --- Pass 1: Clear collision count ---
            {
                auto &clear_srb = clear_resource_binding->GetShaderResourceBinding();
                clear_srb.BindBuffer("Target", *gpu_collision_count);
                clear_srb.BindBuffer("ElemCount", *gpu_one);

                auto *clear_stage_ptr = clear_stage.get();
                auto *clear_binding_ptr = clear_resource_binding;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("ConvexDetect ClearCount")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(count_h, WW)
                        .UseBuffer(one_h, RR)
                        .SetPassFunction(
                            [clear_stage_ptr, clear_binding_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                cb.BindComputeStage(*clear_stage_ptr);
                                cb.BindComputeResource(*clear_binding_ptr);
                                cb.DispatchCompute(1, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // --- Pass 2: MPR collision detection ---
            {
                auto &detect_srb = detect_resource_binding->GetShaderResourceBinding();
                detect_srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                detect_srb.BindBuffer("ShapeType", *gpu.shape_type);
                detect_srb.BindBuffer("ShapeFeature", *gpu.shape_feature);
                detect_srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
                detect_srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);
                detect_srb.BindBuffer("CollisionPairs", *cached_pair_buffer);
                detect_srb.BindBuffer("PairCount", *cached_pair_count_buffer);
                detect_srb.BindBuffer("CollisionIds", *gpu_collision_ids);
                detect_srb.BindBuffer("CollisionNormals", *gpu_collision_normals);
                detect_srb.BindBuffer("ContactPointA", *gpu_contact_point_a);
                detect_srb.BindBuffer("ContactPointB", *gpu_contact_point_b);
                detect_srb.BindBuffer("CollisionCount", *gpu_collision_count);
                detect_srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                detect_srb.BindBuffer("DetectorConfig", *gpu_detector_config);

                const uint32_t detect_wg = std::max(1u, (max_input_collision_pairs + 63u) / 64u);
                auto *detect_stage_ptr = detect_stage.get();
                auto *detect_binding = detect_resource_binding;

                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("Convex Collision Detection")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(shape_alive_h, RR)
                        .UseBuffer(shape_type_h, RR)
                        .UseBuffer(shape_feature_h, RR)
                        .UseBuffer(shape_wpos_h, RR)
                        .UseBuffer(shape_wrot_h, RR)
                        .UseBuffer(pair_h, RR)
                        .UseBuffer(pair_cnt_h, RR)
                        .UseBuffer(ids_h, WW)
                        .UseBuffer(normals_h, WW)
                        .UseBuffer(cpa_h, WW)
                        .UseBuffer(cpb_h, WW)
                        .UseBuffer(count_h, RW)
                        .UseBuffer(slot_cnt_h, RR)
                        .UseBuffer(cfg_h, RR)
                        .SetPassFunction(
                            [detect_stage_ptr,
                             detect_binding,
                             detect_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                                cb.BindComputeStage(*detect_stage_ptr);
                                cb.BindComputeResource(*detect_binding);
                                cb.DispatchCompute(detect_wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            return builder.BuildRenderGraph();
        }
    };

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    ConvexCollisionDetector::ConvexCollisionDetector(RenderSystem &render_system) :
        m_impl(std::make_unique<Impl>(render_system)) {
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
        const ComputeBuffer &pair_buffer,
        const ComputeBuffer &pair_count_buffer
    ) {
        m_impl->cached_scene = &scene;
        m_impl->cached_pair_buffer = &pair_buffer;
        m_impl->cached_pair_count_buffer = &pair_count_buffer;
        m_impl->max_input_collision_pairs = std::max(1u, max_input_collision_pairs);
        m_impl->max_output_collision_pairs = std::max(1u, max_output_collision_pairs);
        m_impl->contact_margin = contact_margin;

        m_impl->EnsureBuffers();
        m_impl->EnsureShadersLoaded();

        // Upload CPU data to GPU-visible buffers.
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

    CollisionResultBuffers ConvexCollisionDetector::Detect(vk::CommandBuffer cb) {
        assert(m_impl->cached_scene && "Configure must be called before Detect");
        const auto gpu = m_impl->cached_scene->GetGpuBuffers();

        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || gpu.shape_slot_count == 0u) {
            CollisionResultBuffers empty;
            empty.max_output_collision_pairs = m_impl->max_output_collision_pairs;
            return empty;
        }

        // Lazy RG creation / rebuild when max_input_collision_pairs changes.
        if (!m_impl->render_graph || m_impl->max_input_collision_pairs != m_impl->cached_max_pairs_for_rg) {
            m_impl->render_graph = m_impl->BuildRenderGraph();
            m_impl->cached_max_pairs_for_rg = m_impl->max_input_collision_pairs;
        }

        if (m_impl->render_graph && m_impl->render_graph->GetNumPasses() > 0) {
            m_impl->render_graph->RecordAllPasses(cb);
        }

        CollisionResultBuffers result;
        result.collision_ids = m_impl->gpu_collision_ids.get();
        result.collision_normals = m_impl->gpu_collision_normals.get();
        result.contact_point_a = m_impl->gpu_contact_point_a.get();
        result.contact_point_b = m_impl->gpu_contact_point_b.get();
        result.collision_count = m_impl->gpu_collision_count.get();
        result.max_output_collision_pairs = m_impl->max_output_collision_pairs;
        return result;
    }
} // namespace Engine
