#include "XPBDGpuSolver.h"

#include "PhysicsScene.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

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
     *
     * @param relative_path Path relative to the physics SPIR-V root, e.g.
     *                      "solver/XPBDSolver/step.comp.spv".
     *
     * @return The SPIR-V words.
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

    struct XPBDGpuSolver::Impl {
        RenderSystem &render_system;

        bool initialized = false;

        // Owned compute pipeline — created once by EnsureInitialized().
        std::unique_ptr<ComputeStage> compute_stage{};

        // Non-owning pointer into compute_stage.
        ComputeResourceBinding *resource_binding = nullptr;

        // Cached compiled SPIR-V so we never recompile.
        std::vector<uint32_t> cached_spirv{};

        // Model matrix update compute pipeline.
        std::unique_ptr<ComputeStage> model_matrix_compute_stage{};
        ComputeResourceBinding *model_matrix_resource_binding = nullptr;
        std::vector<uint32_t> model_matrix_cached_spirv{};

        explicit Impl(RenderSystem &rs) : render_system(rs) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /**
         * @brief Lazily load the compute shaders and create the pipelines.
         *
         * Idempotent — only does work on the first call.
         */
        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            // Load the precompiled position update shader.
            cached_spirv = LoadPhysicsSpirv("solver/XPBDSolver/step.comp.spv");
            compute_stage = std::make_unique<ComputeStage>(render_system);
            compute_stage->Instantiate(cached_spirv, "Physics XPBD Placeholder Step");
            resource_binding = &compute_stage->AllocateResourceBinding();

            // Load the precompiled model matrix update shader.
            model_matrix_cached_spirv = LoadPhysicsSpirv("solver/XPBDSolver/model_matrix.comp.spv");
            model_matrix_compute_stage = std::make_unique<ComputeStage>(render_system);
            model_matrix_compute_stage->Instantiate(model_matrix_cached_spirv, "Physics XPBD Model Matrix");
            model_matrix_resource_binding = &model_matrix_compute_stage->AllocateResourceBinding();
        }
    };

    XPBDGpuSolver::XPBDGpuSolver(RenderSystem &render_system) : m_impl(std::make_unique<Impl>(render_system)) {
    }

    XPBDGpuSolver::~XPBDGpuSolver() = default;

    bool XPBDGpuSolver::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void XPBDGpuSolver::Step(
        RenderGraphBuilder &builder, PhysicsScene &physics_scene, RGBufferHandle external_model_matrices_handle
    ) {
        const auto gpu = physics_scene.GetGpuBuffers();

        // Bail if essential buffers are not yet created.
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_center_world_position == nullptr
            || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        // --- Lazy initialization (first call only) ---
        m_impl->EnsureInitialized();

        // --- Refresh shader resource bindings ---
        // PhysicsScene may recreate GPU buffers between calls (e.g. via
        // RefreshGpuBuffers()), so we rebind every Step().  Descriptor sets are
        // pooled by ComputeStage so rebinding is cheap — no reallocation.
        auto &srb = m_impl->resource_binding->GetShaderResourceBinding();
        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
        srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);

        // --- Import external resources into the render graph ---
        // ComputeBuffer inherits DeviceBuffer, so the const DeviceBuffer& overload
        // of ImportExternalResource matches directly.
        auto alive_handle = builder.ImportExternalResource(*gpu.rigid_body_alive, {MemoryAccessTypeBufferBits::None});
        auto position_handle =
            builder.ImportExternalResource(*gpu.rigid_body_center_world_position, {MemoryAccessTypeBufferBits::None});

        // --- Add the XPBD compute pass ---
        // Capture raw pointers (not references) — the solver owns compute_stage
        // so these outlive the lambda stored in the render graph pass.
        auto *cstage = m_impl->compute_stage.get();
        auto *cbinding = m_impl->resource_binding;

        // Capture slot_count by value.  For the transitional placeholder this is
        // acceptable; the test has a fixed rigid body count after initialization.
        // TODO: replace with indirect dispatch or per-frame UBO update.
        uint32_t slot_count = gpu.rigid_body_slot_count;

        builder.AddPass(
            RenderGraphPassBuilder{m_impl->render_system}
                .SetName("XPBD Step")
                .UseBuffer(alive_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(
                    position_handle,
                    {MemoryAccessTypeBufferBits::ShaderRandomRead, MemoryAccessTypeBufferBits::ShaderRandomWrite}
                )
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction(
                    [&physics_scene, cstage, cbinding, slot_count](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*cstage);
                        cb.BindComputeResource(*cbinding);
                        cb.DispatchCompute((slot_count + 63u) / 64u, 1, 1);
                    }
                )
                .Get()
        );

        // --- Model matrix update compute pass ---
        // Runs after the position update to build mat4 model matrices from the
        // updated position and rotation buffers.  These matrices are then read
        // by vertex shaders via the scene descriptor set (set 0, binding 2).
        if (gpu.model_matrices != nullptr && gpu.rigid_body_center_world_rotation != nullptr) {
            auto &mm_srb = m_impl->model_matrix_resource_binding->GetShaderResourceBinding();
            mm_srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
            mm_srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            mm_srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            mm_srb.BindBuffer("ModelMatrices", *gpu.model_matrices);

            auto rotation_handle = builder.ImportExternalResource(
                *gpu.rigid_body_center_world_rotation, {MemoryAccessTypeBufferBits::None}
            );
            // Use the externally-provided handle if available, otherwise import internally.
            auto model_matrices_handle =
                external_model_matrices_handle != RGBufferHandle{}
                    ? external_model_matrices_handle
                    : builder.ImportExternalResource(*gpu.model_matrices, {MemoryAccessTypeBufferBits::None});

            auto *mm_cstage = m_impl->model_matrix_compute_stage.get();
            auto *mm_cbinding = m_impl->model_matrix_resource_binding;

            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Model Matrix Update")
                    .UseBuffer(alive_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(position_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(rotation_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(model_matrices_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .SetPassFunction(
                        [mm_cstage, mm_cbinding, slot_count](CommandBuffer &cb, const RenderGraph &) -> void {
                            cb.BindComputeStage(*mm_cstage);
                            cb.BindComputeResource(*mm_cbinding);
                            cb.DispatchCompute((slot_count + 63u) / 64u, 1, 1);
                        }
                    )
                    .Get()
            );
        }
    }
} // namespace Engine
