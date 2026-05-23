#ifndef ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED
#define ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Engine {
    class PhysicsScene;

    /**
     * @brief Physics scene manager at engine-system scope.
     *
     * PhysicsSystem owns one PhysicsScene per engine Scene ID and provides
     * create/destroy/query operations for scene lifecycle integration.
     */
    class PhysicsSystem {
    public:
        /**
         * @brief Construct the physics system.
         */
        PhysicsSystem();

        /**
         * @brief Destroy the physics system.
         */
        ~PhysicsSystem();

        /**
         * @brief Disable copy construction.
         */
        PhysicsSystem(const PhysicsSystem &) = delete;

        /**
         * @brief Disable copy assignment.
         *
         * @return This object.
         */
        PhysicsSystem &operator=(const PhysicsSystem &) = delete;

        /**
         * @brief Disable move construction.
         */
        PhysicsSystem(PhysicsSystem &&) = delete;

        /**
         * @brief Disable move assignment.
         *
         * @return This object.
         */
        PhysicsSystem &operator=(PhysicsSystem &&) = delete;

        /**
         * @brief Create or return a physics scene for a scene ID.
         *
         * @param scene_id Engine scene ID.
         * @return Reference to the created or existing physics scene.
         */
        PhysicsScene &CreateScene(uint32_t scene_id);

        /**
         * @brief Destroy the physics scene owned by a scene ID.
         *
         * @param scene_id Engine scene ID.
         */
        void DestroyScene(uint32_t scene_id);

        /**
         * @brief Get mutable physics scene pointer by scene ID.
         *
         * @param scene_id Engine scene ID.
         * @return Scene pointer, or nullptr if not found.
         */
        PhysicsScene *GetScenePtr(uint32_t scene_id);

        /**
         * @brief Get const physics scene pointer by scene ID.
         *
         * @param scene_id Engine scene ID.
         * @return Const scene pointer, or nullptr if not found.
         */
        const PhysicsScene *GetScenePtr(uint32_t scene_id) const;

    private:
        std::unordered_map<uint32_t, std::shared_ptr<PhysicsScene>> m_scene_map{};
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED
