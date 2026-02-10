#include "ImmutableResourceCache.h"

#include "Render/DebugUtils.h"

#include <vulkan/vulkan.hpp>
#include <unordered_map>

namespace {
    /**
     * @brief FNV hasher taken from VALVE fossilze lib
     */
    struct Hasher {
        constexpr static size_t C = 0x100000001b3ull;
        size_t s = 0xcbf29ce484222325ull;

        template <typename T>
        inline void binary_data(const T *data, size_t size) noexcept {
            size /= sizeof(*data);
            for (size_t i = 0; i < size; i++)
                s = (s * C) ^ data[i];
        }

        inline void u32(uint32_t value) noexcept {
            s = (s * C) ^ value;
        }

        /**
         * @brief Hashing an enumeration.
         */
        template <typename T> requires std::is_enum_v<T>
        inline void e(T value) noexcept {
            static_assert(
                std::is_convertible_v<std::underlying_type_t<T>, uint32_t>
            );
            u32(static_cast<uint32_t>(value));
        }

        /**
         * @brief Hashing a Vulkan-Hpp flag.
         */
        template <typename T>
        inline void f(vk::Flags<T> value) noexcept {
            static_assert(
                std::is_convertible_v<typename vk::Flags<T>::MaskType, uint32_t>
            );
            u32(static_cast<vk::Flags<T>::MaskType>(value));
        }

        inline void s32(int32_t value) noexcept {
            u32(uint32_t(value));
        }

        inline void f32(float value) noexcept {
            u32(std::bit_cast<uint32_t>(value));
        }

        inline void u64(uint64_t value) noexcept {
            u32(value & 0xffffffffu);
            u32(value >> 32);
        }

        inline void string(const char *str) noexcept {
            char c;
            u32(0xff);
            while ((c = *str++) != '\0')
                u32(uint8_t(c));
        }

        inline void string(std::string_view str) noexcept {
            u32(0xff);
            for (auto &c : str)
                u32(uint8_t(c));
        }

        inline auto get() const {
            return s;
        }
    };
}

namespace Engine::RenderSystemState {

    struct ImmutableResourceCache::impl {

        vk::Device dvc;

        std::unordered_map <size_t, vk::UniqueSampler> sampler_cache;
        std::unordered_map <size_t, vk::UniqueDescriptorSetLayout> descriptor_set_layout_cache;
        std::unordered_map <size_t, vk::UniquePipelineLayout> pipeline_layout_cache;

        // Use the raw Vulkan version to avoid pulling in the huge `vulkan_hash.hpp`
        std::unordered_map <VkSampler, size_t> sampler_rlut;
        std::unordered_map <VkDescriptorSetLayout, size_t> descriptor_set_layout_rlut;
        std::unordered_map <VkPipelineLayout, size_t> pipeline_layout_rlut;

        auto hash_sampler_create_info(const vk::SamplerCreateInfo & sci) noexcept {
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

        auto hash_descriptor_set_layout(const vk::DescriptorSetLayoutCreateInfo & dsli) noexcept {
            assert(dsli.pNext == nullptr);

            Hasher h;
            h.u32(dsli.bindingCount);
            h.f(dsli.flags);
            for (uint32_t i = 0; i < dsli.bindingCount; i++)
            {
                auto &binding = dsli.pBindings[i];
                h.u32(binding.binding);
                h.u32(binding.descriptorCount);
                h.e(binding.descriptorType);
                h.f(binding.stageFlags);

                if (binding.pImmutableSamplers &&
                    (binding.descriptorType == vk::DescriptorType::eCombinedImageSampler ||
                    binding.descriptorType == vk::DescriptorType::eSampler))
                {
                    for (uint32_t j = 0; j < binding.descriptorCount; j++)
                    {
                        auto itr = sampler_rlut.find(binding.pImmutableSamplers[j]);
                        assert(itr != sampler_rlut.end());
                        h.u64(itr->second);
                    }
                }
            }
            return h.get();
        }

        auto hash_pipeline_layout(const vk::PipelineLayoutCreateInfo & plci) noexcept {
            Hasher h;
            h.u32(plci.setLayoutCount);
            for (uint32_t i = 0; i < plci.setLayoutCount; i++)
            {
                if (plci.pSetLayouts[i])
                {
                    auto itr = descriptor_set_layout_rlut.find(plci.pSetLayouts[i]);
                    assert(itr != descriptor_set_layout_rlut.end());
                    h.u64(itr->second);
                }
                else
                    h.u32(0);
            }

            h.u32(plci.pushConstantRangeCount);
            for (uint32_t i = 0; i < plci.pushConstantRangeCount; i++)
            {
                auto &push = plci.pPushConstantRanges[i];
                h.f(push.stageFlags);
                h.u32(push.size);
                h.u32(push.offset);
            }

            h.f(plci.flags);
            return h.get();
        }
    };

    ImmutableResourceCache::ImmutableResourceCache(
        vk::Device dvc
    ) : pimpl(std::make_unique<impl>()) {
        pimpl->dvc = dvc;
    }

    ImmutableResourceCache::~ImmutableResourceCache() noexcept = default;

    vk::Sampler ImmutableResourceCache::GetSampler(const ImageUtils::SamplerDesc & desc) {
        vk::SamplerCreateInfo sci {
            vk::SamplerCreateFlags{},
            ImageUtils::ToVkFilter(desc.min_filter),
            ImageUtils::ToVkFilter(desc.max_filter),
            ImageUtils::ToVkSamplerMipmapMode(desc.mipmap_filter),
            ImageUtils::ToVkSamplerAddressMode(desc.u_address),
            ImageUtils::ToVkSamplerAddressMode(desc.v_address),
            ImageUtils::ToVkSamplerAddressMode(desc.w_address),
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
        };
        return GetSampler(sci);
    }
    vk::Sampler ImmutableResourceCache::GetSampler(const vk::SamplerCreateInfo &sci) {
        auto h = pimpl->hash_sampler_create_info(sci);
        auto itr = pimpl->sampler_cache.find(h);

        if (itr == pimpl->sampler_cache.end()) {
            auto & r = pimpl->sampler_cache[h];
            r = pimpl->dvc.createSamplerUnique(sci);
            pimpl->sampler_rlut[r.get()] = h;
            return r.get();
        }
        return itr->second.get();
    }
    vk::DescriptorSetLayout ImmutableResourceCache::GetDescriptorSetLayout(
        const vk::DescriptorSetLayoutCreateInfo & dlci, const char *name
    ) {
        auto h = pimpl->hash_descriptor_set_layout(dlci);
        auto itr = pimpl->descriptor_set_layout_cache.find(h);

        if (itr == pimpl->descriptor_set_layout_cache.end()) {
            auto & r = pimpl->descriptor_set_layout_cache[h];
            r = pimpl->dvc.createDescriptorSetLayoutUnique(dlci);
            pimpl->descriptor_set_layout_rlut[r.get()] = h;

            if (name) {
                DEBUG_SET_NAME_TEMPLATE(
                    pimpl->dvc, 
                    r.get(), 
                    name
                );
            }
            return r.get();
        }
        return itr->second.get();
    }
    vk::PipelineLayout ImmutableResourceCache::GetPipelineLayout(
        const vk::PipelineLayoutCreateInfo & plci, const char *name
    ) {
        auto h = pimpl->hash_pipeline_layout(plci);
        auto itr = pimpl->pipeline_layout_cache.find(h);

        if (itr == pimpl->pipeline_layout_cache.end()) {
            auto & r = pimpl->pipeline_layout_cache[h];
            r = pimpl->dvc.createPipelineLayoutUnique(plci);
            pimpl->pipeline_layout_rlut[r.get()] = h;

            if (name) {
                DEBUG_SET_NAME_TEMPLATE(
                    pimpl->dvc, 
                    r.get(), 
                    name
                );
            }
            return r.get();
        }
        return itr->second.get();
    }
} // namespace Engine::RenderSystemState
