#include "DummySolver.h"

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsDispatch.h>
#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSpirvLoader.h>
#include <Physics/Solver/XPBDGpuSolver.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>

#include <algorithm>
#include <cassert>

namespace Engine {

    struct DummySolver::Impl {
        Rhi::DeviceContext &device_context;

        XpbdConfig config{};
        bool initialized = false;

        Rhi::ComputeKernel *compute_kernel = nullptr;

        // The shared model matrix shader, used by GPUCalcModelMatrices. The
        // displacement step does not write model matrices.
        Rhi::ComputeKernel *model_matrix_kernel = nullptr;

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /// @brief Acquire both compute kernels on first use.
        void EnsureLoaded() {
            if (initialized) return;

            compute_kernel =
                &LoadPhysicsKernel(device_context, "solver/DummySolver/dummy_solver.comp.spv", "DummySolver");
            model_matrix_kernel =
                &LoadPhysicsKernel(device_context, "solver/common/model_matrix.comp.spv", "DummySolver ModelMatrix");

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

    void DummySolver::GPUStep(vk::CommandBuffer cb) {
        const auto gpu = m_bound_scene->GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        // Kernel acquisition happens here, before the call's first dispatch; a
        // second step acquires nothing.
        m_impl->EnsureLoaded();

        DispatchBarrier(cb);

        const uint32_t body_wg = (gpu.rigid_body_slot_count + 63u) / 64u;

        const float effective_dt = m_bound_scene->IsSimulationEnabled() ? m_impl->config.time_step : 0.0f;
        const glm::vec4 gravity_dt =
            glm::vec4(m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, effective_dt);

        m_impl->compute_kernel->Dispatch(
            cb,
            {{"RigidBodyAlive", *gpu.rigid_body_alive},
             {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
             {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation}},
            body_wg,
            1,
            1,
            gravity_dt
        );
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
        DispatchBarrier(cb);

        m_impl->model_matrix_kernel->Dispatch(
            cb,
            {{"RigidBodyAlive", *gpu.rigid_body_alive},
             {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
             {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
             {"ModelMatrices", target}},
            (write_count + 63u) / 64u,
            1,
            1
        );
    }

} // namespace Engine
