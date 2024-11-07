#ifndef PIPELINE_PREMADEPIPELINE_SKINNEDCONFIGURABLEPIPELINE_INCLUDED
#define PIPELINE_PREMADEPIPELINE_SKINNEDCONFIGURABLEPIPELINE_INCLUDED

#include "ConfigurablePipeline.h"

namespace Engine {
    class SkinnedConfigurablePipeline : public ConfigurablePipeline {
    public:
        SkinnedConfigurablePipeline(std::weak_ptr <RenderSystem> system);
        virtual ~SkinnedConfigurablePipeline() = default;
        virtual void CreatePipeline() override;
    };
}

#endif // PIPELINE_PREMADEPIPELINE_SKINNEDCONFIGURABLEPIPELINE_INCLUDED
