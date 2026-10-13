#ifndef FRAMEWORK_TOOLS_SHADOWCASCADERESOURCES_INCLUDED
#define FRAMEWORK_TOOLS_SHADOWCASCADERESOURCES_INCLUDED

#include "Framework/framework_export.h"
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>

#include <vector>

namespace Engine {
    class RenderGraph;
    class RenderGraphBuilder;
    class RenderSystem;

    /**
     * @brief Request shadow map storage and build the shadow cascade passes of a render graph.
     *
     * One D32SFLOAT array texture with one layer per cascade is requested per
     * shadow-casting directional light present in the main scene, and one pass is
     * added per light. A pass draws the scene's shadow casters once per cascade,
     * rendering into that cascade's array layer and handing the light's cascade
     * slot (`light * MAX_SHADOW_CASCADES + cascade`) to the shadow map shader.
     *
     * The render graph is built once, so the storage is sized for the lights that
     * exist now. A light added afterwards has no shadow map of its own and is
     * rendered unshadowed (with a warning) instead of sampling unallocated storage.
     *
     * @param builder        Render graph builder receiving the passes.
     * @param system         Render system the graph belongs to.
     * @param model_matrices Model matrices buffer the shadow pass reads.
     * @return One texture handle per shadow-casting light, in light slot order.
     */
    FRAMEWORK_API std::vector<RGTextureHandle> AddShadowCascadePasses(
        RenderGraphBuilder &builder, RenderSystem &system, RGBufferHandle model_matrices
    );

    /**
     * @brief Register a built render graph's shadow maps with the scene data manager.
     *
     * Every shadow map slot is cleared before the new maps are registered, so a
     * graph rebuilt after the previous one was destroyed cannot leave the scene
     * data manager pointing at destroyed storage.
     *
     * @param render_graph  Graph the shadow maps belong to.
     * @param system        Render system owning the scene data manager.
     * @param shadow_map_ids Handles returned by `AddShadowCascadePasses`.
     */
    FRAMEWORK_API void RegisterShadowCascadeMaps(
        RenderGraph &render_graph, RenderSystem &system, const std::vector<RGTextureHandle> &shadow_map_ids
    );
} // namespace Engine

#endif // FRAMEWORK_TOOLS_SHADOWCASCADERESOURCES_INCLUDED
