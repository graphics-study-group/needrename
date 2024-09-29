#ifndef RENDER_PIPELINE_SHADER_INCLUDED
#define RENDER_PIPELINE_SHADER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>

namespace Engine
{
    class ShaderModule : public VkWrapper<vk::UniqueShaderModule>
    {
    public:
        enum class ShaderType {
            None,
            Vertex,
            Fragment
        };

        ShaderModule(std::weak_ptr <RenderSystem> system);
        void CreateShaderModule(std::byte * data, size_t size, ShaderType type);

        vk::PipelineShaderStageCreateInfo GetStageCreateInfo() const;
    protected:
        ShaderType m_type {ShaderType::None};
    };
} // namespace Engine


#endif // RENDER_PIPELINE_SHADER_INCLUDED
