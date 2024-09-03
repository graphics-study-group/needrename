#ifndef RENDER_PIPELINE_PREMADEPIPELINE_SINGLERENDERPASSWITHDEPTH_INCLUDED
#define RENDER_PIPELINE_PREMADEPIPELINE_SINGLERENDERPASSWITHDEPTH_INCLUDED

#include "Render/Pipeline/RenderPass.h"

namespace Engine {

    /// @brief A premade render pass with only one attachment and only one subpass
    class SingleRenderPassWithDepth : public RenderPass
    {
    public:
        SingleRenderPassWithDepth(std::weak_ptr <RenderSystem> system);
        void CreateRenderPass();
    
    private:
        // Hide setting functions
        using RenderPass::SetAttachments;
        using RenderPass::SetDependencies;
        using RenderPass::SetSubpasses;
    };
};

#endif // RENDER_PIPELINE_PREMADEPIPELINE_SINGLERENDERPASSWITHDEPTH_INCLUDED
