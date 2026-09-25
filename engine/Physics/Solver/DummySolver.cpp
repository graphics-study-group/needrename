#include "DummySolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Physics/Solver/XPBDGpuSolver.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeHelpers.h>

#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Pipeline/ComputeResourceBinding.h>
#include <Rhi/Pipeline/ComputeStage.h>
#include <Rhi/Pipeline/ShaderResourceBinding.h>

#include <algorithm>
#include <cassert>
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
        Rhi::DeviceContext &device_context;

        XpbdConfig config{};
        bool initialized = false;

        std::unique_ptr<Rhi::ComputeStage> compute_stage{};
        std::vector<uint32_t> shader_spirv{};
        Rhi::ComputeResourceBinding *resource_binding = nullptr;

        // The shared model matrix shader, used by GPUCalcModelMatrices. The
        // displacement step does not write model matrices.
        std::unique_ptr<Rhi::ComputeStage> model_matrix_stage{};
        std::vector<uint32_t> model_matrix_spirv{};
        Rhi::ComputeResourceBinding *model_matrix_binding = nullptr;

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /// @brief Load both compute stages on first use.
        void EnsureLoaded() {
            if (initialized) return;

            shader_spirv = LoadPhysicsSpirv("solver/DummySolver/dummy_solver.comp.spv");
            compute_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            compute_stage->Instantiate(shader_spirv, "DummySolver");
            resource_binding = &compute_stage->AllocateResourceBinding();

            model_matrix_spirv = LoadPhysicsSpirv("solver/common/model_matrix.comp.spv");
            model_matrix_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            model_matrix_stage->Instantiate(model_matrix_spirv, "DummySolver ModelMatrix");
            model_matrix_binding = &model_matrix_stage->AllocateResourceBinding();

            initialized = true;
        }
    };

    DummySolver::DummySolver(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
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
            m_impl->EnsureLoaded();
        }
    }

    void DummySolver::GPUStep(vk::CommandBuffer cb) {
        const auto gpu = m_bound_scene->GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        auto &srb = m_impl->resource_binding->GetShaderResourceBinding();
        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
        srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
        srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);

        const uint32_t body_wg = (gpu.rigid_body_slot_count + 63u) / 64u;

        const float effective_dt = m_bound_scene->IsSimulationEnabled() ? m_impl->config.time_step : 0.0f;
        const glm::vec4 gravity_dt =
            glm::vec4(m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, effective_dt);

        Rhi::PushConstants(cb, *m_impl->compute_stage, gravity_dt);
        Rhi::BindComputeStage(cb, *m_impl->compute_stage);
        Rhi::BindComputeResource(cb, *m_impl->compute_stage, *m_impl->resource_binding);
        Rhi::DispatchCompute(cb, body_wg, 1, 1);
    }

    void DummySolver::GPUCalcModelMatrices(vk::CommandBuffer cb, Rhi::ComputeBuffer &target) {
        const auto gpu = m_bound_scene->GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) {
            return;
        }
        if (gpu.rigid_body_center_world_position == nullptr || gpu.rigid_body_center_world_rotation == nullptr) {
            return;
        }

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t target_capacity = static_cast<uint32_t>(target.GetSize() / sizeof(glm::mat4));

        // The caller owns the capacity contract: report a shortfall in debug
        // builds and clamp in release rather than writing out of bounds.
        assert(
            body_count <= target_capacity
            && "GPUCalcModelMatrices target is smaller than the scene's rigid body slot count"
        );
        const uint32_t write_count = std::min(body_count, target_capacity);
        if (write_count == 0u) {
            return;
        }

        m_impl->EnsureLoaded();

        // The production is separate from the step, so it records the barrier
        // that makes the poses it reads visible.
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        auto &srb = m_impl->model_matrix_binding->GetShaderResourceBinding();
        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
        srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
        srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
        srb.BindBuffer("ModelMatrices", target);

        Rhi::BindComputeStage(cb, *m_impl->model_matrix_stage);
        Rhi::BindComputeResource(cb, *m_impl->model_matrix_stage, *m_impl->model_matrix_binding);
        Rhi::DispatchCompute(cb, (write_count + 63u) / 64u, 1, 1);
    }

} // namespace Engine
