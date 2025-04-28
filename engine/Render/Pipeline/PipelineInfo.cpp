#include "PipelineInfo.h"

#include <cassert>
#include <SDL3/SDL.h>
#include <glm.hpp>

#include "Render/Memory/ImageInterface.h"

namespace Engine::PipelineInfo {
    void PlaceUBOVariables(const std::unordered_map<uint32_t,std::any>& variables, const PassInfo & info, std::vector<std::byte>& memory)
    {
        if (memory.size() < info.uniforms.maximal_ubo_size) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                "Performing buffer allocation for UBO layout."
            );
            memory.resize(info.uniforms.maximal_ubo_size);
        }

        for (const auto & [idx, var] : variables) {
            assert(idx < info.uniforms.variables.size() && "Uniform variable index is too large.");
            const auto offset = info.uniforms.variables[idx].location.offset;
            try {
                using Type = ShaderVariable::Type;
                switch(info.uniforms.variables[idx].type) {
                case Type::Int:
                    *(reinterpret_cast<int*>(memory.data() + offset)) = std::any_cast<int>(var);
                    break;
                case Type::Float:
                    *(reinterpret_cast<float*>(memory.data() + offset)) = std::any_cast<float>(var);
                    break;
                case Type::Vec4:
                    // Let's hope it works...
                    *(reinterpret_cast<glm::vec4*>(memory.data() + offset)) = std::any_cast<glm::vec4>(var);
                    break;
                case Type::Mat4:
                    *(reinterpret_cast<glm::mat4*>(memory.data() + offset)) = std::any_cast<glm::mat4>(var);
                    break;
                default:
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unexpected uniform variable type.");
                }
            } catch(std::bad_any_cast & e) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Mismatched uniform type of uniform index %u", idx);
                continue;
            }
        }
    }
    std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> 
    GetDescriptorImageInfo(
        const std::unordered_map<uint32_t, std::any> &variables, 
        const PassInfo &info,
        vk::Sampler sampler
    )
    {
        std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> ret;

        for (size_t idx = 0; idx < info.uniforms.variables.size(); idx++) {
            const auto& uniform = info.uniforms.variables[idx];
            if (uniform.type == ShaderVariable::Type::Texture) {
                if (!variables.contains(idx)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Texture variable %llu not found in instance.", idx);
                    continue;
                }

                std::shared_ptr<const ImageInterface> image{};
                try {
                    image = (std::any_cast<std::shared_ptr<const ImageInterface>> (variables.at(idx)));
                } catch (std::exception & e) {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Variable %llu is not a texture, or the texture is invalid.", idx);
                    continue;
                }

                vk::DescriptorImageInfo image_info {};
                image_info.imageView = image->GetImageView();
                image_info.imageLayout = vk::ImageLayout::eReadOnlyOptimal;
                image_info.sampler = sampler;
                ret.push_back(std::make_pair(uniform.location.binding, image_info));
            }
        }
        return ret;
    }
}
