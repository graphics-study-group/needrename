#ifndef RENDER_PIPELINE_SHADER_INCLUDED
#define RENDER_PIPELINE_SHADER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>

namespace Engine
{
    class ShaderModule : public VkWrapper<vk::UniqueShaderModule>
    {
    public:
        ShaderModule(std::weak_ptr <RenderSystem> system);

        void CreateShaderModule(const std::vector<char> & data);

        vk::PipelineShaderStageCreateInfo GetStageCreateInfo(vk::ShaderStageFlagBits stageBit);
    
    };
} // namespace Engine


#endif // RENDER_PIPELINE_SHADER_INCLUDED
