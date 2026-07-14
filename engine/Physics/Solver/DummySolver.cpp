#include "DummySolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Physics/Solver/XpbdGpuSolver.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
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

    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };
} // namespace

namespace Engine {

    struct DummySolver::Impl {
        RenderSystem &render_system;

        XpbdConfig config{};
        bool initialized = false;

        std::unique_ptr<ComputeStage> compute_stage{};
        std::vector<uint32_t> shader_spirv{};
        ComputeResourceBinding *resource_binding = nullptr;

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
            (void)body_count;
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

    void DummySolver::PreGPUStep() {
        const auto gpu = m_bound_scene->GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        if (!m_impl->initialized) {
            m_impl->shader_spirv = LoadPhysicsSpirv("solver/DummySolver/dummy_solver.comp.spv");
            m_impl->compute_stage = std::make_unique<ComputeStage>(m_impl->render_system);
            m_impl->compute_stage->Instantiate(m_impl->shader_spirv, "DummySolver");
            m_impl->resource_binding = &m_impl->compute_stage->AllocateResourceBinding();

            m_impl->render_system.GetSceneDataManager().SetModelMatricesBuffer(gpu.model_matrices);
            m_impl->initialized = true;
        }

        const uint32_t body_count = gpu.rigid_body_slot_count;

        m_impl->EnsureUniformBuffer(body_count);

        float effective_dt = m_bound_scene->IsSimulationEnabled() ? m_impl->config.time_step : 0.0f;
        auto *uniform_addr = reinterpret_cast<glm::vec4 *>(m_impl->gpu_uniforms->GetVMAddress());
        *uniform_addr =
            glm::vec4(m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, effective_dt);
    }

    void DummySolver::GPUStep(CommandBuffer &command_buffer) {
        const auto gpu = m_bound_scene->GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        command_buffer.GetCommandBuffer().pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        auto &srb = m_impl->resource_binding->GetShaderResourceBinding();
        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
        srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
        srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
        srb.BindBuffer("DummySolverUniforms", *m_impl->gpu_uniforms);
        srb.BindBuffer("ModelMatrices", *gpu.model_matrices);

        const uint32_t body_wg = (gpu.rigid_body_slot_count + 63u) / 64u;

        command_buffer.BindComputeStage(*m_impl->compute_stage);
        command_buffer.BindComputeResource(*m_impl->resource_binding);
        command_buffer.DispatchCompute(body_wg, 1, 1);
    }

} // namespace Engine
