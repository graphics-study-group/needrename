#ifndef ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED
#define ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Solver/ISolver.h"

namespace vk {
    struct CommandBuffer;
}

namespace Engine {
    class PhysicsScene;
    class RenderSystem;

    /**
     * @brief Physics scene manager at engine-system scope.
     *
     * PhysicsSystem owns one PhysicsScene per engine Scene ID and provides
     * create/destroy/query operations for scene lifecycle integration.
     *
     * Solvers are registered per-scene via RegisterSolver(scene_id, ...) and
     * bound to their target scene through ISolver::OnBindToScene() at
     * registration time. The three-phase PreGPUStep / GPUStep / PostGPUStep
     * iterate all scenes and dispatch to each scene's registered solvers.
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

        /**
         * @brief Register a GPU physics solver for a specific scene.
         *
         * The solver is bound to the scene via OnBindToScene() and stored
         * in registration order. If the scene does not exist, a warning is
         * logged and the solver is discarded.
         *
         * @param scene_id Engine scene ID to bind the solver to.
         * @param solver   Unique-ownership solver instance.
         */
        void RegisterSolver(uint32_t scene_id, std::unique_ptr<ISolver> solver);

        /**
         * @brief CPU-side preparation before GPU work.
         *
         * Calls PreGPUStep on each registered solver for every scene.
         * Must be called BEFORE cb.begin().
         */
        void PreGPUStep();

        /**
         * @brief GPU work — solvers record RenderGraph passes to cb.
         *
         * Calls GPUStep on each registered solver for every scene.
         * Must be called BETWEEN cb.begin() and cb.end().
         *
         * @param cb CommandBuffer in Recording state.
         */
        void GPUStep(vk::CommandBuffer cb);

        /**
         * @brief Post-GPU work (readback, cleanup).
         *
         * Calls PostGPUStep on each registered solver for every scene.
         * Must be called AFTER cb.end() + submit.
         */
        void PostGPUStep();

    private:
        std::unordered_map<uint32_t, std::shared_ptr<PhysicsScene>> m_scene_map{};
        std::unordered_map<uint32_t, std::vector<std::unique_ptr<ISolver>>> m_solvers_per_scene{};
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED
