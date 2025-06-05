#include "PipelineInfo.h"

#include <cassert>
#include <SDL3/SDL.h>
#include <glm.hpp>

#include "Render/Memory/SampledTexture.h"

namespace Engine::PipelineInfo {
    void PlaceUBOVariables(
        const std::unordered_map<uint32_t, std::any>& variables, 
        const PassInfo & info,
        std::vector<std::byte>& memory
    ) noexcept
    {
        if (memory.size() < info.inblock.maximal_ubo_size) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                "Performing buffer allocation for UBO layout."
            );
            memory.resize(info.inblock.maximal_ubo_size);
        }

        for (const auto & [idx, var] : variables) {
            assert(idx < info.inblock.vars.size() && "Uniform variable index is too large.");
            const auto offset = info.inblock.vars[idx].inblock_location.offset;
    
            using Type = ShaderVariable::Type;
            using UBOType = ShaderInBlockVariableProperty::InBlockVarType;
            switch(info.inblock.vars[idx].type) {
            case UBOType::Int:
                assert(var.type() == typeid(int));
                *(reinterpret_cast<int*>(memory.data() + offset)) = *std::any_cast<int>(&var);
                break;
            case UBOType::Float:
                assert(var.type() == typeid(float));
                *(reinterpret_cast<float*>(memory.data() + offset)) = *std::any_cast<float>(&var);
                break;
            case UBOType::Vec4:
                // Let's hope it works...
                assert(var.type() == typeid(glm::vec4));
                *(reinterpret_cast<glm::vec4*>(memory.data() + offset)) = *std::any_cast<glm::vec4>(&var);
                break;
            case UBOType::Mat4:
                assert(var.type() == typeid(glm::mat4));
                *(reinterpret_cast<glm::mat4*>(memory.data() + offset)) = *std::any_cast<glm::mat4>(&var);
                break;
            default:
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unexpected uniform variable type.");
            }
        }
    }
    std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> 
    GetDescriptorImageInfo(
        const std::unordered_map<uint32_t, std::any> &variables, 
        const PassInfo &info,
        vk::Sampler sampler
    ) noexcept
    {
        std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> ret;

        for (size_t idx = 0; idx < info.desc.vars.size(); idx++) {
            const auto& uniform = info.desc.vars[idx];
            const auto& instance_var = variables.find(idx);
            if (uniform.type == ShaderVariable::Type::Texture) {
                if (instance_var == variables.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Texture variable %llu not found in instance.", idx);
                    continue;
                }

                assert(
                    instance_var->second.type() == typeid(std::shared_ptr<const SampledTexture>) 
                    && "Sampled image variable is not a sampled image, or the image is invalid."
                );
                std::shared_ptr<const SampledTexture> image{
                    *std::any_cast<std::shared_ptr<const SampledTexture>> (&instance_var->second)
                };
                assert((image->GetSampler() || sampler) && "Sampler not supplied for sampled image.");

                vk::DescriptorImageInfo image_info {};
                image_info.imageView = image->GetImageView();
                image_info.imageLayout = vk::ImageLayout::eReadOnlyOptimal;
                image_info.sampler = image->GetSampler() ? image->GetSampler() : sampler;
                ret.push_back(std::make_pair(uniform.binding, image_info));
            } else if (uniform.type == ShaderVariable::Type::StorageImage) {
                if (instance_var == variables.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Storage image variable %llu not found in instance.", idx);
                    continue;
                }

                assert(
                    instance_var->second.type() == typeid(std::shared_ptr<const Texture>) && 
                    "Storage image variable is not a texture image, or the texture is invalid."
                );
                std::shared_ptr<const Texture> image{
                    *std::any_cast<std::shared_ptr<const Texture>> (&instance_var->second)
                };

                vk::DescriptorImageInfo image_info {};
                image_info.imageView = image->GetImageView();
                image_info.imageLayout = vk::ImageLayout::eGeneral;
                image_info.sampler = nullptr;
                ret.push_back(std::make_pair(uniform.binding, image_info));
            }
        }
        return ret;
    }
    std::vector<std::pair<uint32_t,vk::DescriptorBufferInfo>> 
    GetDescriptorBufferInfo(
        const std::unordered_map<uint32_t,std::any>& variables, 
        const PassInfo & info
    ) noexcept
    {
        std::vector<std::pair<uint32_t, vk::DescriptorBufferInfo>> ret;

        for (size_t idx = 0; idx < info.desc.vars.size(); idx++) {
            const auto& uniform = info.desc.vars[idx];
            const auto& instance_var = variables.find(idx);
        }
        return ret;
    }
}
