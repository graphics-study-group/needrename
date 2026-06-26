#include "PhysicsSystem.h"

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

    void PhysicsSystem::RegisterSolver(std::unique_ptr<ISolver> solver) {
        m_solvers.push_back(std::move(solver));
    }

    void PhysicsSystem::PreGPUStep(RenderSystem &render_system) {
        auto *scene = GetScenePtr(1);
        if (scene == nullptr) {
            return;
        }
        for (auto &solver : m_solvers) {
            solver->PreGPUStep(render_system, *scene);
        }
    }

    void PhysicsSystem::GPUStep(RenderSystem &render_system, vk::CommandBuffer cb) {
        auto *scene = GetScenePtr(1);
        if (scene == nullptr) {
            return;
        }
        for (auto &solver : m_solvers) {
            solver->GPUStep(render_system, *scene, cb);
        }
    }

    void PhysicsSystem::PostGPUStep(RenderSystem &render_system) {
        auto *scene = GetScenePtr(1);
        if (scene == nullptr) {
            return;
        }
        for (auto &solver : m_solvers) {
            solver->PostGPUStep(render_system, *scene);
        }
    }
} // namespace Engine
