#ifndef FRAMEWORK_TOOLS_COMPLEXRENDERGRAPHBUILDER_INCLUDED
#define FRAMEWORK_TOOLS_COMPLEXRENDERGRAPHBUILDER_INCLUDED

#include "Asset/AssetRef.h"
#include "Framework/framework_export.h"
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>

#include <memory>

namespace Engine {
    namespace Rhi {
        class ComputeBuffer;
        class ComputeKernel;
    } // namespace Rhi
    class RenderGraph;
    class RenderSystem;
    class GUISystem;

    /**
     * @brief A render graph that integrates all current rendering features (Shadow, PBR, Blinn-Phong, etc.)
     * TODO: Need better way to manage the render graph.
     */
    class FRAMEWORK_API ComplexRenderGraphBuilder {
    public:
        ComplexRenderGraphBuilder(RenderSystem &system);
        ~ComplexRenderGraphBuilder() = default;

        /**
         * @brief Build the default rendering render graph.
         *
         * Render-time dimensions (viewport, rendering area, compute dispatch) are
         * derived at record time from the currently-resolved textures or the
         * present provider extent, so resizing does not require rebuilding the graph.
         *
         * @param final_color_target_id     Output handle for the final (post-bloom) color target.
         * @param background_color          Clear color of the HDR color target, in linear rgb.
         * @param gui_overlay               Optional ImGui overlay. When non-null, a pass that
         *                                  draws the GUI on top of the final color target is
         *                                  appended; the caller must have called
         *                                  `GUISystem::PrepareGUI` and built its ImGui windows
         *                                  for the frame before recording the graph, and the
         *                                  backend must have been created with the final color
         *                                  target's format.
         * @return Compiled RenderGraph.
         */
        std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(
            RGTextureHandle &final_color_target_id,
            const AttachmentUtils::ColorClearValue &background_color = {},
            GUISystem *gui_overlay = nullptr
        );

    protected:
        RenderSystem &m_system;
        AssetRef m_bloom_shader{};
        /// @brief The bloom kernel, owned by the device context.
        Rhi::ComputeKernel *m_bloom_kernel{};
    };
} // namespace Engine

#endif // FRAMEWORK_TOOLS_COMPLEXRENDERGRAPHBUILDER_INCLUDED
