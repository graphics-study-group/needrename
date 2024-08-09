#include "Shader.h"

namespace Engine
{
    ShaderModule::ShaderModule(std::weak_ptr<RenderSystem> system) : VkWrapper(system) {}

    void ShaderModule::CreateShaderModule(const std::vector<char>& data) 
    {
        vk::ShaderModuleCreateInfo info;
        info.codeSize = data.size();
        info.pCode = reinterpret_cast<const uint32_t*>(data.data());
        this->m_handle = m_system.lock()->getDevice().createShaderModuleUnique(info);
    }

    vk::PipelineShaderStageCreateInfo ShaderModule::GetStageCreateInfo(
        vk::ShaderStageFlagBits stageBit) {
        vk::PipelineShaderStageCreateInfo info;
        info.stage = stageBit;
        info.module = m_handle.get();
        info.pName = "main";
        return info;
    }

} // namespace Engine

