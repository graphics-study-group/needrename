#ifndef ENGINE_RHI_IMMUTABLERESOURCECACHE_INCLUDED
#define ENGINE_RHI_IMMUTABLERESOURCECACHE_INCLUDED

#include "Rhi/Device/ResourceRef.h"
#include "Rhi/rhi_export.h"

#include <vulkan/vulkan.hpp>

#include <memory>

namespace vk {
    class Device;
    class Sampler;
    class DescriptorSetLayout;
    class PipelineLayout;
} // namespace vk

namespace Engine::Rhi {
    struct SamplerDesc;

    /**
     * @brief Cache for immutable resources such as samplers, descriptor set
     * layouts and so on.
     *
     * @see Fossilze library by VALVE. Hashing of Vulkan create info
     * structures are largely based on it.
     */
    class RHI_API ImmutableResourceCache {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        /// @brief Create the cache on the given device.
        ImmutableResourceCache(vk::Device dvc);
        ~ImmutableResourceCache() noexcept;

        /**
         * @brief Request a sampler from a sampler description
         *
         * If multiple border colors are specified in the descriptor,
         * the first one will be used.
         */
        vk::Sampler GetSampler(const Rhi::SamplerDesc &);
        /// @overload vk::Sampler GetSampler(const Rhi::SamplerDesc &)
        vk::Sampler GetSampler(const vk::SamplerCreateInfo &);

        /**
         * @brief Request a sampler together with the identity it was issued at
         * creation.
         *
         * The identity is the sampler's binding identity: its handle value is
         * irrelevant to comparison. Repeated requests of one description yield
         * one sampler and one identity.
         */
        SamplerRef GetSamplerRef(const Rhi::SamplerDesc &);
        /// @overload SamplerRef GetSamplerRef(const Rhi::SamplerDesc &)
        SamplerRef GetSamplerRef(const vk::SamplerCreateInfo &);

        /**
         * @brief Request a descriptor set layout.
         *
         * Immutable samplers will be hashed directly. This should be fine
         * so long as all samplers are managed by this cache.
         */
        vk::DescriptorSetLayout GetDescriptorSetLayout(
            const vk::DescriptorSetLayoutCreateInfo &, const char *name = nullptr
        );

        /**
         * @brief Request a descriptor set layout together with the identity it
         * was issued at creation.
         *
         * Repeated requests of one description yield one layout and one
         * identity; the identity is what a descriptor set is keyed on.
         */
        DescriptorSetLayoutRef GetDescriptorSetLayoutRef(
            const vk::DescriptorSetLayoutCreateInfo &, const char *name = nullptr
        );

        /**
         * @brief Request a pipeline layout.
         *
         * Descriptor set layouts will be hashed directly. This should be
         * fine so long as all descriptor set layouts are managed by this
         * cache.
         */
        vk::PipelineLayout GetPipelineLayout(const vk::PipelineLayoutCreateInfo &, const char *name = nullptr);
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_IMMUTABLERESOURCECACHE_INCLUDED
