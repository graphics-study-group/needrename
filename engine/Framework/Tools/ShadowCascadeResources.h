#ifndef FRAMEWORK_TOOLS_SHADOWCASCADERESOURCES_INCLUDED
#define FRAMEWORK_TOOLS_SHADOWCASCADERESOURCES_INCLUDED

#include "Framework/framework_export.h"
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>

#include <vector>

namespace Engine {
    class RenderGraphBuilder;
    class RenderSystem;

    /**
     * @brief Borrow the shadow map storage and build the shadow cascade passes of a render graph.
     *
     * The storage is owned by the render system's scene data manager and exists
     * for every budgeted shadow-casting light slot. This function imports all of
     * them into the graph and adds one pass per slot. A pass draws the scene's
     * shadow casters once per cascade of its slot's light, rendering into that
     * cascade's array layer and handing the light's cascade slot
     * (`light * MAX_SHADOW_CASCADES + cascade`) to the shadow map shader.
     *
     * Because the graph only borrows the storage, nothing here depends on the
     * scene: a light added or removed after the graph was built starts or stops
     * casting shadows on the next frame, and rebuilding the graph neither
     * reallocates nor re-registers anything. Slots without a light (or whose
     * light has no fitted cascades yet) simply draw nothing.
     *
     * @param builder        Render graph builder receiving the passes.
     * @param system         Render system the graph belongs to.
     * @param model_matrices Model matrices buffer the shadow pass reads.
     * @return One texture handle per budgeted shadow-casting light slot, in slot
     *         order.
     */
    FRAMEWORK_API std::vector<RGTextureHandle> AddShadowCascadePasses(
        RenderGraphBuilder &builder, RenderSystem &system, RGBufferHandle model_matrices
    );
} // namespace Engine

#endif // FRAMEWORK_TOOLS_SHADOWCASCADERESOURCES_INCLUDED
