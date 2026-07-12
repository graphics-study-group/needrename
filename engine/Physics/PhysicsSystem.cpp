#include "PhysicsSystem.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

#include "PhysicsScene.h"
#include "Solver/ISolver.h"

namespace Engine {
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

    void PhysicsSystem::PreGPUStep() {
        for (auto &[scene_id, scene] : m_scene_map) {
            auto iter = m_solvers_per_scene.find(scene_id);
            if (iter == m_solvers_per_scene.end()) {
                continue;
            }
            for (auto &solver : iter->second) {
                solver->PreGPUStep();
            }
        }
    }

    void PhysicsSystem::GPUStep(CommandBuffer &cb) {
        for (auto &[scene_id, scene] : m_scene_map) {
            auto iter = m_solvers_per_scene.find(scene_id);
            if (iter == m_solvers_per_scene.end()) {
                continue;
            }
            for (auto &solver : iter->second) {
                solver->GPUStep(cb);
            }
        }
    }

    void PhysicsSystem::PostGPUStep() {
        for (auto &[scene_id, scene] : m_scene_map) {
            auto iter = m_solvers_per_scene.find(scene_id);
            if (iter == m_solvers_per_scene.end()) {
                continue;
            }
            for (auto &solver : iter->second) {
                solver->PostGPUStep();
            }
        }
    }

} // namespace Engine
