#ifndef RENDER_RENDERSYSTEM_IMMUTABLERESOURCECACHE_INCLUDED
#define RENDER_RENDERSYSTEM_IMMUTABLERESOURCECACHE_INCLUDED

#include <memory>

namespace vk {
    class Device;
    class Sampler;
    class DescriptorSetLayout;
    class PipelineLayout;
} // namespace vk

namespace Engine {
    namespace ImageUtils {
        class SamplerDesc;
    }

    namespace RenderSystemState {
        /**
         * @brief Cache for immutable resources such as samplers, descriptor set
         * layouts and so on.
         *
         * @see Fossilze library by VALVE. Hashing of Vulkan create info
         * structures are largely based on it.
         */
        class ImmutableResourceCache {
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            ImmutableResourceCache(vk::Device dvc);
            ~ImmutableResourceCache() noexcept;

            /**
             * @brief Request a sampler from a sampler description
             *
             * If multiple border colors are specified in the descriptor,
             * the first one will be used.
             */
            vk::Sampler GetSampler(const ImageUtils::SamplerDesc &);
            /// @overload vk::Sampler GetSampler(const ImageUtils::SamplerDesc &)
            vk::Sampler GetSampler(const vk::SamplerCreateInfo &);

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
             * @brief Request a pipeline layout.
             *
             * Descriptor set layouts will be hashed directly. This should be
             * fine so long as all descriptor set layouts are managed by this
             * cache.
             */
            vk::PipelineLayout GetPipelineLayout(const vk::PipelineLayoutCreateInfo &, const char *name = nullptr);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_IMMUTABLERESOURCECACHE_INCLUDED
