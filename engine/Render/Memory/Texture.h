#ifndef RENDER_MEMORY_TEXTURE_INCLUDED
#define RENDER_MEMORY_TEXTURE_INCLUDED

#include "Render/ImageUtils.h"
#include "Render/Memory/TextureSlice.h"
#include <memory>

namespace vk {
    class Image;
    class ImageView;
    class Sampler;
} // namespace vk

namespace Engine {
    class RenderSystem;
    class AllocatedMemory;
    class Buffer;

    /**
     *  @brief A base class for textures with handles to 
     * allocated GPU resources. You should call named constructors
     * of its derived classes to obtain an instance.
     * 
     * @note Movable but non-copyable.
     */
    class Texture {
    public:
        using TextureDesc = ImageUtils::TextureDesc;
        using SamplerDesc = ImageUtils::SamplerDesc;

    protected:
        RenderSystem &m_system;

        struct impl;
        std::unique_ptr <impl> pimpl;

        Texture(
            RenderSystem & system,
            TextureDesc texture,
            SamplerDesc sampler,
            const std::string & name = ""
        );

    public:
        
        virtual ~Texture();
        /**
         * @brief Get the description struct of this texture.
         */
        const TextureDesc & GetTextureDescription() const noexcept;

        /**
         * @brief Get the description struct of the sampler of this texture.
         */
        const SamplerDesc & GetSamplerDescription() const noexcept;

        /**
         * @brief Get the underlying handle of this texture.
         */
        vk::Image GetImage() const noexcept;

        /**
         * @brief Get the underlying handle of the sampler.
         */
        vk::Sampler GetSampler() const noexcept;

        /**
         * @brief Get a slice referring to the whole range
         * of its subresources (i.e. mipmaps and array layers)
         */
        const SlicedTextureView &GetFullSlice() const noexcept;

        /**
         * @brief Get the underlying handle of the full texture slice.
         */
        vk::ImageView GetImageView() const noexcept;

        /**
         * @brief Acquire a buffer large enough to hold the whole texture.
         */
        Buffer CreateStagingBuffer() const;
    };
} // namespace Engine

#endif // RENDER_MEMORY_TEXTURE_INCLUDED
