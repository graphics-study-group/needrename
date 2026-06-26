#include "DummySolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Physics/Solver/XPBDGpuSolver.h> // for XpbdConfig
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
#include <Render/RenderSystem/SceneDataManager.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
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

    struct DummySolver::Impl {
        RenderSystem &render_system;

        XpbdConfig config{};
        bool initialized = false;

        // Cached body count for RG rebuild detection.
        uint32_t cached_body_count = 0;

        // Solver-owned RenderGraph (lazily built).
        std::unique_ptr<RenderGraph> render_graph{};

        // Compute stage and SPIR-V.
        std::unique_ptr<ComputeStage> compute_stage{};
        std::vector<uint32_t> shader_spirv{};

        // Host-visible uniform buffer: vec4(gravity.xyz, time_step).
        std::unique_ptr<ComputeBuffer> gpu_uniforms{};

        explicit Impl(RenderSystem &rs) : render_system(rs) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureUniformBuffer(uint32_t body_count) {
            size_t sz = sizeof(glm::vec4);
            if (!gpu_uniforms || gpu_uniforms->GetSize() != sz) {
                const auto &alloc = render_system.GetAllocatorState();
                gpu_uniforms =
                    ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "DummySolver Uniforms");
            }
            (void)body_count; // not needed for uniform buffer size
        }
    };

    DummySolver::DummySolver(RenderSystem &render_system) : m_impl(std::make_unique<Impl>(render_system)) {
    }

    DummySolver::~DummySolver() = default;

    bool DummySolver::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void DummySolver::SetConfig(const XpbdConfig &config) noexcept {
        m_impl->config = config;
    }

    const XpbdConfig &DummySolver::GetConfig() const noexcept {
        return m_impl->config;
    }

    void DummySolver::PreGPUStep(RenderSystem &system, PhysicsScene &scene) {
        const auto gpu = scene.GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        // ---- Lazy shader initialization ----
        if (!m_impl->initialized) {
            m_impl->shader_spirv = LoadPhysicsSpirv("solver/DummySolver/dummy_solver.comp.spv");
            m_impl->compute_stage = std::make_unique<ComputeStage>(system);
            m_impl->compute_stage->Instantiate(m_impl->shader_spirv, "DummySolver");
            m_impl->initialized = true;
        }

        const uint32_t body_count = gpu.rigid_body_slot_count;

        // Ensure uniform buffer exists.
        m_impl->EnsureUniformBuffer(body_count);

        // Write uniforms to host-visible buffer.
        // When simulation is disabled, time_step = 0 so no displacement
        // occurs, but model matrices are still written.
        {
            float effective_dt = scene.IsSimulationEnabled() ? m_impl->config.time_step : 0.0f;
            auto *uniform_addr = reinterpret_cast<glm::vec4 *>(m_impl->gpu_uniforms->GetVMAddress());
            *uniform_addr = glm::vec4(
                m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, effective_dt
            );
        }
    }

    void DummySolver::GPUStep(RenderSystem &system, PhysicsScene &scene, vk::CommandBuffer cb) {
        const auto gpu = scene.GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        // ---- Lazy RG creation ----
        if (!m_impl->render_graph || gpu.rigid_body_slot_count != m_impl->cached_body_count) {
            m_impl->render_graph = BuildRenderGraph(system, scene);
            m_impl->cached_body_count = gpu.rigid_body_slot_count;

            // Notify SceneDataManager about model matrices buffer.
            system.GetSceneDataManager().SetModelMatricesBuffer(gpu.model_matrices);
        }

        // Record physics passes to the shared command buffer.
        if (m_impl->render_graph && m_impl->render_graph->GetNumPasses() > 0) {
            m_impl->render_graph->RecordAllPasses(cb);
        }
    }

    std::unique_ptr<RenderGraph> DummySolver::BuildRenderGraph(RenderSystem &system, PhysicsScene &scene) {
        const auto gpu = scene.GetGpuBuffers();

        RenderGraphBuilder builder{system};

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t body_wg = (body_count + 63u) / 64u;

        // Import physics scene buffers once each.
        using AT = MemoryAccessTypeBufferBits;
        const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
        const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};

        auto alive_h = builder.ImportExternalResource(*gpu.rigid_body_alive, {AT::None});
        auto pos_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_position, {AT::None});
        auto rot_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_rotation, {AT::None});
        auto mm_h = builder.ImportExternalResource(*gpu.model_matrices, {AT::None});
        auto uniforms_h = builder.ImportExternalResource(*m_impl->gpu_uniforms, {AT::None});

        // Allocate resource binding and bind buffers.
        auto *binding = &m_impl->compute_stage->AllocateResourceBinding();
        auto &srb = binding->GetShaderResourceBinding();
        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
        srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
        srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
        srb.BindBuffer("DummySolverUniforms", *m_impl->gpu_uniforms);
        srb.BindBuffer("ModelMatrices", *gpu.model_matrices);

        auto *stage = m_impl->compute_stage.get();

        builder.AddPass(
            RenderGraphPassBuilder{system}
                .SetName("DummySolver Step")
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .UseBuffer(alive_h, RR)
                .UseBuffer(pos_h, RW)
                .UseBuffer(rot_h, RR)
                .UseBuffer(uniforms_h, RR)
                .UseBuffer(mm_h, {AT::ShaderRandomWrite})
                .SetPassFunction([stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                    cb.BindComputeStage(*stage);
                    cb.BindComputeResource(*binding);
                    cb.DispatchCompute(body_wg, 1, 1);
                })
                .Get()
        );

        return builder.BuildRenderGraph();
    }

} // namespace Engine
