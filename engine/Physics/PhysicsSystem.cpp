#include "PhysicsSystem.h"

#include "PhysicsScene.h"

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
} // namespace Engine
