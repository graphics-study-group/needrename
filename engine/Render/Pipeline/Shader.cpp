#include "Shader.h"
#include "Render/RenderSystem.h"

#include <SDL3/SDL.h>

namespace Engine
{
    ShaderModule::ShaderModule(std::weak_ptr<RenderSystem> system) : VkWrapper(system) {}

    void ShaderModule::CreateShaderModule(std::byte* data, size_t size, ShaderType type) {
        vk::ShaderModuleCreateInfo info;
        info.codeSize = size;
        info.pCode = reinterpret_cast<const uint32_t*>(data);
        this->m_handle = m_system.lock()->getDevice().createShaderModuleUnique(info);

        m_type = type;
    }

    vk::PipelineShaderStageCreateInfo ShaderModule::GetStageCreateInfo() const {
        vk::PipelineShaderStageCreateInfo info;
        switch (m_type) {
        case ShaderType::Fragment:
            info.stage = vk::ShaderStageFlagBits::eFragment;
            break;
        case ShaderType::Vertex:
            info.stage = vk::ShaderStageFlagBits::eVertex;
            break;
        case ShaderType::None:
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Unspecified shader stage, defaulting to all.");
            info.stage = vk::ShaderStageFlagBits::eAll;
        }
        info.module = m_handle.get();
        info.pName = "main";
        return info;
    }

} // namespace Engine

