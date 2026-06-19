#ifndef EXAMPLE_PHYSICS_EXAMPLE_PHYSICSEXAMPLERENDERGRAPHBUILDER_H
#define EXAMPLE_PHYSICS_EXAMPLE_PHYSICSEXAMPLERENDERGRAPHBUILDER_H

#include "Asset/AssetRef.h"
#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"

#include <memory>

namespace Engine {
    class ComputeStage;
    class PhysicsScene;
    class RenderGraph;
    class RenderSystem;
    class XPBDGpuSolver;
} // namespace Engine

/**
 * @brief Builds a complete render graph for the physics example.
 *
 * Integrates XPBD physics compute passes with the full rendering pipeline:
 * shadow maps → lit pass (with physics model matrices) → bloom → skybox.
 *
 * The XPBD solver owns collision detection internally — no separate
 * ConvexCollisionDetector is needed by the caller.
 *
 * Usage:
 *   PhysicsExampleRenderGraphBuilder builder(*render_system);
 *   RGTextureHandle final_color_id;
 *   auto rg = builder.BuildRenderGraph(w, h, *physics_scene, final_color_id);
 *   cmc->SetRenderGraph(std::move(rg), final_color_id);
 */
class PhysicsExampleRenderGraphBuilder {
    static const uint32_t SHADOWMAP_WIDTH = 2048;
    static const uint32_t SHADOWMAP_HEIGHT = 2048;

public:
    explicit PhysicsExampleRenderGraphBuilder(Engine::RenderSystem &system);
    ~PhysicsExampleRenderGraphBuilder();

    PhysicsExampleRenderGraphBuilder(const PhysicsExampleRenderGraphBuilder &) = delete;
    PhysicsExampleRenderGraphBuilder &operator=(const PhysicsExampleRenderGraphBuilder &) = delete;
    PhysicsExampleRenderGraphBuilder(PhysicsExampleRenderGraphBuilder &&) = delete;
    PhysicsExampleRenderGraphBuilder &operator=(PhysicsExampleRenderGraphBuilder &&) = delete;

    std::unique_ptr<Engine::RenderGraph> BuildRenderGraph(
        uint32_t texture_width,
        uint32_t texture_height,
        Engine::PhysicsScene &physics_scene,
        Engine::RGTextureHandle &final_color_target_id
    );

private:
    Engine::RenderSystem &m_system;
    std::unique_ptr<Engine::XPBDGpuSolver> m_xpbd_solver;
    Engine::AssetRef m_bloom_shader{};
    std::shared_ptr<Engine::ComputeStage> m_bloom_compute_stage{};
};

#endif // EXAMPLE_PHYSICS_EXAMPLE_PHYSICSEXAMPLERENDERGRAPHBUILDER_H
