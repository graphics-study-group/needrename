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
        void CreatePipelineLayout (const std::vector<vk::DescriptorSetLayout> & set, const std::vector<vk::PushConstantRange> & push);

        /// @brief Create a layout with descriptor sets as follows:
        /// 0. Per-scene constant descriptors;
        /// 1. Per-camera constant descriptors;
        /// 2. Per-material constant descriptors;
        /// 3. Per-mesh constant descriptors, if the mesh is skinned.
        /// @param material_descriptor_set 2nd descriptor set
        void CreateWithDefault (vk::DescriptorSetLayout material_descriptor_set, bool skinned = false);
    };
} // namespace Engine


#endif // RENDER_PIPELINE_PIPELINELAYOUT_INCLUDED
