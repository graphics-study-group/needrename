#ifndef RENDER_RENDERSYSTEM_IMMUTABLERESOURCECACHE_INCLUDED
#define RENDER_RENDERSYSTEM_IMMUTABLERESOURCECACHE_INCLUDED

#include <memory>

namespace vk {
    class Device;
    class Sampler;
    class DescriptorSetLayout;
    class PipelineLayout;
}

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
            std::unique_ptr <impl> pimpl;

        public:
            ImmutableResourceCache(vk::Device dvc);
            ~ImmutableResourceCache() noexcept;

            vk::Sampler GetSampler(
                const ImageUtils::SamplerDesc &
            );
            vk::Sampler GetSampler(
                const vk::SamplerCreateInfo &
            );

            vk::DescriptorSetLayout GetDescriptorSetLayout(
                const vk::DescriptorSetLayoutCreateInfo &,
                const char * name = nullptr
            );

            vk::PipelineLayout GetPipelineLayout(
                const vk::PipelineLayoutCreateInfo &,
                const char * name = nullptr
            );
        };
    }
}

#endif // RENDER_RENDERSYSTEM_IMMUTABLERESOURCECACHE_INCLUDED
