#ifndef RENDER_MEMORY_TEXTURE_INCLUDED
#define RENDER_MEMORY_TEXTURE_INCLUDED

#include "Rhi/ImageUtils.h"

#include <memory>
#include <string>

namespace vk {
    class Image;
    class ImageView;
    class Sampler;
} // namespace vk

namespace Engine::Rhi {
    class AllocatedMemory;
    class AllocatorState;
    class DeviceBuffer;
    class DeviceContext;
    struct TextureSubresourceRange;

    /**
     *  @brief A base class for textures with handles to
     * allocated GPU resources. You should call named constructors
     * of its derived classes to obtain an instance.
     *
     * @note Movable but non-copyable.
     */
    class Texture {
    public:
        using TextureDesc = Engine::Rhi::TextureDesc;
        using SamplerDesc = Engine::Rhi::SamplerDesc;

    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

        // Used in move operator for copy-and-swap
        Texture();

        Texture(DeviceContext &device_context, TextureDesc texture, SamplerDesc sampler, const std::string &name = "");

    public:
        Texture(const Texture &) = delete;
        void operator=(const Texture &) = delete;

        Texture(Texture &&) noexcept;
        // I don't want to rewrite the whole reference mess, so just delete it.
        Texture &operator=(Texture &&) noexcept = delete;

        virtual ~Texture();
        /**
         * @brief Get the description struct of this texture.
         */
        const TextureDesc &GetTextureDescription() const noexcept;

        /**
         * @brief Get the description struct of the sampler of this texture.
         */
        const SamplerDesc &GetSamplerDescription() const noexcept;

        /**
         * @brief Get the underlying handle of this texture.
         */
        vk::Image GetImage() const noexcept;

        /**
         * @brief Get the underlying handle of the sampler.
         */
        vk::Sampler GetSampler() const noexcept;

        /**
         * @brief Get the underlying handle of the full texture subresource view.
         */
        vk::ImageView GetImageView() const;

        /**
         * @brief Get the underlying handle of a texture subresource view.
         */
        vk::ImageView GetImageView(const TextureSubresourceRange &tsr) const;

        /**
         * @brief Calculate the size of staging buffer required to hold the
         * entire texture.
         *
         * It does not consider mipmap levels of the texture. Only the first
         * level is considered.
         */
        size_t CalculateStagingBufferSizeNoMipmap() const noexcept;

        /**
         * @brief Whether this texture supports random access (UAV
         * for HLSL, storage image for GLSL).
         */
        virtual bool SupportRandomAccess() const noexcept;

        /**
         * @brief Whether this texture supports atomic ops.
         */
        virtual bool SupportAtomicOperation() const noexcept;
    };
} // namespace Engine::Rhi

#endif // RENDER_MEMORY_TEXTURE_INCLUDED
