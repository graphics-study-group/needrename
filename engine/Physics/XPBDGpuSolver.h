#ifndef ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
#define ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED

namespace Engine {
    class PhysicsScene;
    class RenderSystem;

    /**
     * @brief Placeholder XPBD GPU solver.
     *
     * The solver owns no per-scene simulation state. It consumes GPU buffers
     * directly from PhysicsScene on each step.
     */
    class XPBDGpuSolver {
    public:
        static void Step(RenderSystem &render_system, PhysicsScene &physics_scene);
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED