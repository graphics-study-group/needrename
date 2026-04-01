#include "ImmutableResourceCache.h"

#include "Render/DebugUtils.h"
#include "Render/Hasher.hpp"
#include "Render/Pipeline/PipelineUtils.hpp"

#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace {
    using Hasher = Engine::RenderResourceHasher;

    struct sampler_hasher {
        size_t operator()(const vk::SamplerCreateInfo &sci) const noexcept {
            assert(sci.pNext == nullptr);

            Hasher h{};
            // This flag does not affect actual sampler creation.
            h.f(sci.flags & ~vk::SamplerCreateFlagBits::eDescriptorBufferCaptureReplayEXT);
            h.f32(sci.maxAnisotropy);
            h.f32(sci.mipLodBias);
            h.f32(sci.minLod);
            h.f32(sci.maxLod);
            h.e(sci.minFilter);
            h.e(sci.magFilter);
            h.e(sci.mipmapMode);
            h.u32(sci.compareEnable);
            h.e(sci.compareOp);
            h.u32(sci.anisotropyEnable);
            h.e(sci.addressModeU);
            h.e(sci.addressModeV);
            h.e(sci.addressModeW);
            h.e(sci.borderColor);
            h.u32(sci.unnormalizedCoordinates);

            return h.get();
        }
    };

    struct descriptor_set_layout_hasher {
        size_t operator()(const vk::DescriptorSetLayoutCreateInfo &dsli) const noexcept {
            assert(dsli.pNext == nullptr);

            Hasher h;
            h.u32(dsli.bindingCount);
            h.f(dsli.flags);
            for (uint32_t i = 0; i < dsli.bindingCount; i++) {
                auto &binding = dsli.pBindings[i];
                h.u32(binding.binding);
                h.u32(binding.descriptorCount);
                h.e(binding.descriptorType);
                h.f(binding.stageFlags);

                if (binding.pImmutableSamplers
                    && (binding.descriptorType == vk::DescriptorType::eCombinedImageSampler
                        || binding.descriptorType == vk::DescriptorType::eSampler)) {
                    for (uint32_t j = 0; j < binding.descriptorCount; j++) {
                        h.handle(binding.pImmutableSamplers[j]);
                    }
                }
            }
            return h.get();
        }
    };

    struct pipeline_layout_hasher {
        size_t operator()(const vk::PipelineLayoutCreateInfo &plci) const noexcept {
            Hasher h;
            h.u32(plci.setLayoutCount);
            for (uint32_t i = 0; i < plci.setLayoutCount; i++) {
                if (plci.pSetLayouts[i]) {
                    h.handle(plci.pSetLayouts[i]);
                } else h.u32(0);
            }

            h.u32(plci.pushConstantRangeCount);
            for (uint32_t i = 0; i < plci.pushConstantRangeCount; i++) {
                auto &push = plci.pPushConstantRanges[i];
                h.f(push.stageFlags);
                h.u32(push.size);
                h.u32(push.offset);
            }

            h.f(plci.flags);
            return h.get();
        }
    };
} // namespace

namespace Engine::RenderSystemState {

    struct ImmutableResourceCache::impl {

        vk::Device dvc;

        std::unordered_map<vk::SamplerCreateInfo, vk::UniqueSampler, sampler_hasher> sampler_cache;
        std::unordered_map<size_t, vk::UniqueDescriptorSetLayout> descriptor_set_layout_cache;
        std::unordered_map<size_t, vk::UniquePipelineLayout> pipeline_layout_cache;
    };

    ImmutableResourceCache::ImmutableResourceCache(vk::Device dvc) : pimpl(std::make_unique<impl>()) {
        pimpl->dvc = dvc;
    }

    ImmutableResourceCache::~ImmutableResourceCache() noexcept = default;

    vk::Sampler ImmutableResourceCache::GetSampler(const ImageUtils::SamplerDesc &desc) {
        vk::SamplerCreateInfo sci{
            vk::SamplerCreateFlags{},
            ImageUtils::ToVkFilter(desc.min_filter),
            ImageUtils::ToVkFilter(desc.max_filter),
            ImageUtils::ToVkSamplerMipmapMode(desc.mipmap_filter),
            ImageUtils::ToVkSamplerAddressMode(desc.u_address),
            ImageUtils::ToVkSamplerAddressMode(desc.v_address),
            ImageUtils::ToVkSamplerAddressMode(desc.w_address),
            desc.bias_lod,
            (desc.max_anisotropy > 1.0f),
            desc.max_anisotropy,
            (desc.comparator != ImageUtils::SamplerDesc::DepthComparator::Always),
            PipelineUtils::ToVkCompareOp(desc.comparator),
            desc.min_lod,
            desc.max_lod,
            vk::BorderColor::eFloatTransparentBlack,
            false,
            nullptr
        };

        if (ImageUtils::ToVkSamplerAddressMode(desc.u_address) == vk::SamplerAddressMode::eClampToBorder) {
            sci.borderColor = ImageUtils::ToVkBorderColor(desc.u_address);
        } else if (ImageUtils::ToVkSamplerAddressMode(desc.v_address) == vk::SamplerAddressMode::eClampToBorder) {
            sci.borderColor = ImageUtils::ToVkBorderColor(desc.v_address);
        } else if (ImageUtils::ToVkSamplerAddressMode(desc.w_address) == vk::SamplerAddressMode::eClampToBorder) {
            sci.borderColor = ImageUtils::ToVkBorderColor(desc.w_address);
        }

        return GetSampler(sci);
    }
    vk::Sampler ImmutableResourceCache::GetSampler(const vk::SamplerCreateInfo &sci) {
        auto itr = pimpl->sampler_cache.find(sci);

        if (itr == pimpl->sampler_cache.end()) {
            auto &r = pimpl->sampler_cache[sci];
            r = pimpl->dvc.createSamplerUnique(sci);
            return r.get();
        }
        return itr->second.get();
    }
    vk::DescriptorSetLayout ImmutableResourceCache::GetDescriptorSetLayout(
        const vk::DescriptorSetLayoutCreateInfo &dlci, const char *name
    ) {
        auto h = descriptor_set_layout_hasher{}(dlci);
        auto itr = pimpl->descriptor_set_layout_cache.find(h);

        if (itr == pimpl->descriptor_set_layout_cache.end()) {
            auto &r = pimpl->descriptor_set_layout_cache[h];
            r = pimpl->dvc.createDescriptorSetLayoutUnique(dlci);

            if (name) {
                DEBUG_SET_NAME_TEMPLATE(pimpl->dvc, r.get(), name);
            }
            return r.get();
        }
        return itr->second.get();
    }
    vk::PipelineLayout ImmutableResourceCache::GetPipelineLayout(
        const vk::PipelineLayoutCreateInfo &plci, const char *name
    ) {
        auto h = pipeline_layout_hasher{}(plci);
        auto itr = pimpl->pipeline_layout_cache.find(h);

        if (itr == pimpl->pipeline_layout_cache.end()) {
            auto &r = pimpl->pipeline_layout_cache[h];
            r = pimpl->dvc.createPipelineLayoutUnique(plci);

            if (name) {
                DEBUG_SET_NAME_TEMPLATE(pimpl->dvc, r.get(), name);
            }
            return r.get();
        }
        return itr->second.get();
    }
} // namespace Engine::RenderSystemState
