#ifndef ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED
#define ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vk {
    struct CommandBuffer;
}

namespace Engine {
    class ISolver;
    class PhysicsScene;
    class RenderSystem;

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

        /**
         * @brief Register a GPU physics solver.
         *
         * Solvers are iterated in registration order.
         */
        void RegisterSolver(std::unique_ptr<ISolver> solver);

        /**
         * @brief CPU-side preparation before GPU work.
         *
         * Calls PreGPUStep on each registered solver for the main scene.
         * Must be called BEFORE cb.begin().
         */
        void PreGPUStep(RenderSystem &render_system);

        /**
         * @brief GPU work — solvers record RenderGraph passes to cb.
         *
         * Calls GPUStep on each registered solver for the main scene.
         * Must be called BETWEEN cb.begin() and cb.end().
         */
        void GPUStep(RenderSystem &render_system, vk::CommandBuffer cb);

        /**
         * @brief Post-GPU work (readback, cleanup).
         *
         * Calls PostGPUStep on each registered solver for the main scene.
         * Must be called AFTER cb.end() + submit.
         */
        void PostGPUStep(RenderSystem &render_system);

    private:
        std::unordered_map<uint32_t, std::shared_ptr<PhysicsScene>> m_scene_map{};
        std::vector<std::unique_ptr<ISolver>> m_solvers{};
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSYSTEM_INCLUDED
