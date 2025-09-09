#include "PipelineInfo.h"

#include <SDL3/SDL.h>
#include <cassert>
#include <glm.hpp>

#include "Render/Memory/Texture.h"
#include "Render/Memory/TextureSlice.h"

static std::pair<vk::ImageView, vk::Sampler> ExtractImageViewAndSampler(std::any var) noexcept {
    using namespace Engine;
    if (var.type() == typeid(std::shared_ptr<const Texture>)) {
        auto ptr = std::any_cast<std::shared_ptr<const Texture>>(var);
        return std::make_pair(ptr->GetImageView(), ptr->GetSampler());
    }
    if (var.type() == typeid(std::shared_ptr<const SlicedTextureView>)) {
        auto ptr = std::any_cast<std::shared_ptr<const SlicedTextureView>>(var);
        return std::make_pair(
            ptr->GetImageView(), ptr->GetTexture().GetSampler()
        );
    }

    // Or maybe we should force everyone to use immutable samplers...
    assert(!"A sampled image variable must be typed std::shared_ptr<const Texture> or std::shared_ptr<const "
            "SlicedTextureView>.");
    return std::make_pair(nullptr, nullptr);
}

static vk::ImageView ExtractImageViewForStorageImage(std::any var) noexcept {
    using namespace Engine;
    if (var.type() == typeid(std::shared_ptr<const Texture>)) {
        auto ptr = std::any_cast<std::shared_ptr<const Texture>>(var);
        return ptr->GetImageView();
    }
    if (var.type() == typeid(std::shared_ptr<const SlicedTextureView>)) {
        auto ptr = std::any_cast<std::shared_ptr<const SlicedTextureView>>(var);
        return ptr->GetImageView();
    }

    assert(!"A storage image variable must be typed std::shared_ptr<const Texture> or std::shared_ptr<const "
            "SlicedTextureView>.");
    return nullptr;
}

namespace Engine::PipelineInfo {
    void PlaceUBOVariables(
        const std::unordered_map<uint32_t, std::any> &variables, const PassInfo &info, std::vector<std::byte> &memory
    ) noexcept {
        if (memory.size() < info.inblock.maximal_ubo_size) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Performing buffer allocation for UBO layout.");
            memory.resize(info.inblock.maximal_ubo_size);
        }

        for (const auto &[idx, var] : variables) {
            assert(idx < info.inblock.vars.size() && "Uniform variable index is too large.");
            const auto offset = info.inblock.vars[idx].inblock_location.offset;

            using Type = ShaderVariableProperty::Type;
            using UBOType = ShaderInBlockVariableProperty::InBlockVarType;
            switch (info.inblock.vars[idx].type) {
            case UBOType::Int:
                assert(var.type() == typeid(int));
                *(reinterpret_cast<int *>(memory.data() + offset)) = *std::any_cast<int>(&var);
                break;
            case UBOType::Float:
                assert(var.type() == typeid(float));
                *(reinterpret_cast<float *>(memory.data() + offset)) = *std::any_cast<float>(&var);
                break;
            case UBOType::Vec4:
                // Let's hope it works...
                assert(var.type() == typeid(glm::vec4));
                *(reinterpret_cast<glm::vec4 *>(memory.data() + offset)) = *std::any_cast<glm::vec4>(&var);
                break;
            case UBOType::Mat4:
                assert(var.type() == typeid(glm::mat4));
                *(reinterpret_cast<glm::mat4 *>(memory.data() + offset)) = *std::any_cast<glm::mat4>(&var);
                break;
            default:
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unexpected uniform variable type.");
            }
        }
    }
    std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> GetDescriptorImageInfo(
        const std::unordered_map<uint32_t, std::any> &variables, const PassInfo &info, vk::Sampler sampler
    ) noexcept {
        std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> ret;

        for (size_t idx = 0; idx < info.desc.vars.size(); idx++) {
            const auto &uniform = info.desc.vars[idx];
            const auto &instance_var = variables.find(idx);
            if (uniform.type == ShaderVariableProperty::Type::Texture) {
                if (instance_var == variables.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Texture variable %llu not found in instance.", idx);
                    continue;
                }

                auto [view, img_sampler] = ExtractImageViewAndSampler(instance_var->second);
                assert((img_sampler || sampler) && "Sampler not supplied for sampled image.");

                vk::DescriptorImageInfo image_info{};
                image_info.imageView = view;
                image_info.imageLayout = vk::ImageLayout::eReadOnlyOptimal;
                image_info.sampler = img_sampler ? img_sampler : sampler;
                ret.push_back(std::make_pair(uniform.binding, image_info));
            } else if (uniform.type == ShaderVariableProperty::Type::StorageImage) {
                if (instance_var == variables.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Storage image variable %llu not found in instance.", idx);
                    continue;
                }

                auto view = ExtractImageViewForStorageImage(instance_var->second);

                vk::DescriptorImageInfo image_info{};
                image_info.imageView = view;
                image_info.imageLayout = vk::ImageLayout::eGeneral;
                image_info.sampler = nullptr;
                ret.push_back(std::make_pair(uniform.binding, image_info));
            }
        }
        return ret;
    }
    std::vector<std::pair<uint32_t, vk::DescriptorBufferInfo>> GetDescriptorBufferInfo(
        const std::unordered_map<uint32_t, std::any> &variables, const PassInfo &info
    ) noexcept {
        std::vector<std::pair<uint32_t, vk::DescriptorBufferInfo>> ret;

        for (size_t idx = 0; idx < info.desc.vars.size(); idx++) {
            const auto &uniform = info.desc.vars[idx];
            const auto &instance_var = variables.find(idx);
        }
        return ret;
    }
} // namespace Engine::PipelineInfo
