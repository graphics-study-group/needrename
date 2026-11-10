#include "PhysicsSystem.h"


#include <SDL3/SDL.h>

#include "PhysicsScene.h"
#include "Solver/ISolver.h"
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Command/CommandEncoder.h>

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
         * The clear is a transfer, and `FillBuffer` is where the seam records the
         * dependencies it needs: the solver's compute reads of these buffers are
         * ordered before the fill, and the fill before the next step's compute
         * reads. Nothing here has to name a stage or an access.
         *
         * @param scene   Physics scene whose step just ran.
         * @param encoder Encoder the step was recorded to.
         */
        void RecordExternalWrenchConsumption(PhysicsScene &scene, Rhi::CommandEncoder &encoder) {
            if (!scene.IsSimulationEnabled()) {
                return;
            }
            const auto gpu = scene.GetGpuBuffers();
            if (gpu.rigid_body_external_force == nullptr || gpu.rigid_body_external_torque == nullptr) {
                return;
            }
            const uint64_t force_bytes = gpu.rigid_body_external_force->GetSize();
            const uint64_t torque_bytes = gpu.rigid_body_external_torque->GetSize();
            if (force_bytes == 0u || torque_bytes == 0u) {
                return;
            }

            encoder.FillBuffer(gpu.rigid_body_external_force->GetBuffer(), 0u, force_bytes, 0u);
            encoder.FillBuffer(gpu.rigid_body_external_torque->GetBuffer(), 0u, torque_bytes, 0u);
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

    void PhysicsSystem::GPUStep(Rhi::CommandEncoder &encoder) {
        for (auto &[scene_id, scene] : m_scene_map) {
            auto iter = m_solvers_per_scene.find(scene_id);
            if (iter == m_solvers_per_scene.end()) {
                continue;
            }
            for (auto &solver : iter->second) {
                solver->GPUStep(encoder);
            }
            RecordExternalWrenchConsumption(*scene, encoder);
        }
    }

    void PhysicsSystem::GPUCalcModelMatrices(
        PhysicsScene &scene, Rhi::CommandEncoder &encoder, Rhi::ComputeBuffer &target
    ) {
        auto iter = m_solvers_per_scene.find(scene.GetSceneID());
        if (iter == m_solvers_per_scene.end()) {
            return;
        }
        for (auto &solver : iter->second) {
            solver->GPUCalcModelMatrices(encoder, target);
        }
    }

} // namespace Engine

