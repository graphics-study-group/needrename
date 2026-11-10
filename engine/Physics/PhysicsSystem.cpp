#include "PhysicsSystem.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

#include "PhysicsScene.h"
#include "Solver/ISolver.h"
#include <Rhi/Buffer/ComputeBuffer.h>

namespace Engine {
    namespace {
        /**
         * @brief Record the per-step consumption of external force and torque.
         *
         * The external wrench buffers are per-step quantities: a step's integration
         * applies whatever values they hold, and those values must not act again on
         * a later step. The clearing is recorded once per scene after every solver
         * has run, so one solver's consumption cannot erase values another solver
         * has not seen yet.
         *
         * A step that does not integrate leaves the buffers untouched: pending
         * values are deferred to the next step that actually integrates, matching
         * the write-lifetime contract of the body-write API.
         *
         * @param scene Physics scene whose step just ran.
         * @param cb    Command buffer the step was recorded to.
         */
        void RecordExternalWrenchConsumption(PhysicsScene &scene, vk::CommandBuffer cb) {
            if (!scene.IsSimulationEnabled()) {
                return;
            }
            const auto gpu = scene.GetGpuBuffers();
            if (gpu.rigid_body_external_force == nullptr || gpu.rigid_body_external_torque == nullptr) {
                return;
            }
            const vk::DeviceSize force_bytes = gpu.rigid_body_external_force->GetSize();
            const vk::DeviceSize torque_bytes = gpu.rigid_body_external_torque->GetSize();
            if (force_bytes == 0u || torque_bytes == 0u) {
                return;
            }

            // The step's compute passes read these buffers; the fills overwrite
            // them, so the reads must complete first.
            const vk::MemoryBarrier2 before_fill{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {before_fill}, {}, {}});

            cb.fillBuffer(gpu.rigid_body_external_force->GetBuffer(), 0u, force_bytes, 0u);
            cb.fillBuffer(gpu.rigid_body_external_torque->GetBuffer(), 0u, torque_bytes, 0u);

            // Make the cleared values visible to the next step's compute passes.
            const vk::MemoryBarrier2 after_fill{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {after_fill}, {}, {}});
        }
    } // namespace

    PhysicsSystem::PhysicsSystem() {
    }

    PhysicsSystem::~PhysicsSystem() {
    }

    PhysicsScene &PhysicsSystem::CreateScene(uint32_t scene_id) {
        auto iter = m_scene_map.find(scene_id);
        if (iter != m_scene_map.end()) {
            return *iter->second;
        }

        auto scene = std::make_shared<PhysicsScene>(scene_id);
        auto *scene_ptr = scene.get();
        m_scene_map[scene_id] = std::move(scene);
        return *scene_ptr;
    }

    void PhysicsSystem::DestroyScene(uint32_t scene_id) {
        m_scene_map.erase(scene_id);
    }

    PhysicsScene *PhysicsSystem::GetScenePtr(uint32_t scene_id) {
        auto iter = m_scene_map.find(scene_id);
        if (iter == m_scene_map.end()) {
            return nullptr;
        }
        return iter->second.get();
    }

    const PhysicsScene *PhysicsSystem::GetScenePtr(uint32_t scene_id) const {
        auto iter = m_scene_map.find(scene_id);
        if (iter == m_scene_map.end()) {
            return nullptr;
        }
        return iter->second.get();
    }

    void PhysicsSystem::RegisterSolver(uint32_t scene_id, std::unique_ptr<ISolver> solver) {
        auto *scene = GetScenePtr(scene_id);
        if (scene == nullptr) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "PhysicsSystem::RegisterSolver: scene %u does not exist — solver not registered",
                scene_id
            );
            return;
        }

        solver->OnBindToScene(*scene);
        m_solvers_per_scene[scene_id].push_back(std::move(solver));
    }

    void PhysicsSystem::GPUStep(vk::CommandBuffer cb) {
        for (auto &[scene_id, scene] : m_scene_map) {
            auto iter = m_solvers_per_scene.find(scene_id);
            if (iter == m_solvers_per_scene.end()) {
                continue;
            }
            for (auto &solver : iter->second) {
                solver->GPUStep(cb);
            }
            RecordExternalWrenchConsumption(*scene, cb);
        }
    }

    void PhysicsSystem::GPUCalcModelMatrices(PhysicsScene &scene, vk::CommandBuffer cb, Rhi::ComputeBuffer &target) {
        auto iter = m_solvers_per_scene.find(scene.GetSceneID());
        if (iter == m_solvers_per_scene.end()) {
            return;
        }
        for (auto &solver : iter->second) {
            solver->GPUCalcModelMatrices(cb, target);
        }
    }

} // namespace Engine
