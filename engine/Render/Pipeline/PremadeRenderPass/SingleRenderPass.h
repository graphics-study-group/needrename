#ifndef RENDER_PIPELINE_PREMADERENDERPASS_SINGLERENDERPASS_INCLUDED
#define RENDER_PIPELINE_PREMADERENDERPASS_SINGLERENDERPASS_INCLUDED

#include "Render/Pipeline/RenderPass.h"

namespace Engine {

    /// @brief A premade render pass with only one attachment and only one subpass
    class SingleRenderPass : public RenderPass
    {
    public:
        SingleRenderPass(std::weak_ptr <RenderSystem> system);
        void CreateRenderPass();
    };
};

#endif // RENDER_PIPELINE_PREMADERENDERPASS_SINGLERENDERPASS_INCLUDED
