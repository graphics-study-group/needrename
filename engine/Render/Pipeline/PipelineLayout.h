#ifndef RENDER_PIPELINE_PIPELINELAYOUT_INCLUDED
#define RENDER_PIPELINE_PIPELINELAYOUT_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>

namespace Engine
{
    /// @brief Pipeline layouts define constant data (uniforms, etc.) settings for a pipeline.
    class PipelineLayout : public VkWrapper<vk::UniquePipelineLayout>
    {
    public:
        PipelineLayout(std::weak_ptr <RenderSystem> system);
        void CreatePipelineLayout(const std::vector<vk::DescriptorSetLayout> & set, const std::vector<vk::PushConstantRange> & push);
    };
} // namespace Engine


#endif // RENDER_PIPELINE_PIPELINELAYOUT_INCLUDED
