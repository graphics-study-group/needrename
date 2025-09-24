#include "SamplerManager.h"

#include <vulkan/vulkan.hpp>
#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"

namespace Engine::RenderSystemState {
    struct SamplerManager::impl {
        std::unordered_map <ImageUtils::SamplerDesc, vk::UniqueSampler, ImageUtils::SamplerDesc::Hasher<false>> m_samplers;
    };

    SamplerManager::SamplerManager(RenderSystem &system) : 
        m_system(system), pimpl(std::make_unique<impl>()) {
    }
    SamplerManager::~SamplerManager() {
    }

    vk::Sampler SamplerManager::GetSampler(ImageUtils::SamplerDesc desc) {
        auto itr = pimpl->m_samplers.find(desc);
        if (itr == pimpl->m_samplers.end()) {
            vk::UniqueSampler s = m_system.getDevice().createSamplerUnique(
                vk::SamplerCreateInfo{
                    vk::SamplerCreateFlags{},
                    ToVkFilter(desc.min_filter),
                    ToVkFilter(desc.max_filter),
                    ToVkSamplerMipmapMode(desc.mipmap_filter),
                    ToVkSamplerAddressMode(desc.u_address),
                    ToVkSamplerAddressMode(desc.v_address),
                    ToVkSamplerAddressMode(desc.w_address),
                    desc.bias_lod,
                    false,
                    0.0,
                    false,
                    vk::CompareOp::eNever,
                    desc.min_lod,
                    desc.max_lod,
                    vk::BorderColor::eFloatTransparentBlack,
                    false,
                    nullptr
                }
            );
            DEBUG_SET_NAME_TEMPLATE(
                m_system.getDevice(), 
                s.get(), 
                std::format(
                    "Sampler (filter: {},{}, addresser: {},{},{})",
                    (uint8_t)desc.min_filter, (uint8_t)desc.max_filter,
                    (uint8_t)desc.u_address, (uint8_t)desc.v_address, (uint8_t)desc.w_address
                )
            );
            
            auto ret = pimpl->m_samplers.insert(std::make_pair(desc, std::move(s)));
            itr = ret.first;
        }

        return itr->second.get();
    }

} // namespace Engine::RenderSystemState
