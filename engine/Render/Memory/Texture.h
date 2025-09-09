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

    class Texture {
    public:
        using TextureDesc = ImageUtils::TextureDesc;
        using SamplerDesc = ImageUtils::SamplerDesc;

    protected:
        RenderSystem &m_system;

        struct impl;
        std::unique_ptr <impl> pimpl;

    public:
        Texture(
            RenderSystem & system,
            TextureDesc texture,
            SamplerDesc sampler,
            const std::string & name = ""
        );
        virtual ~Texture();


        const TextureDesc & GetTextureDescription() const noexcept;

        const SamplerDesc & GetSamplerDescription() const noexcept;

        vk::Image GetImage() const noexcept;

        vk::Sampler GetSampler() const noexcept;

        const SlicedTextureView &GetFullSlice() const noexcept;

        vk::ImageView GetImageView() const noexcept;

        Buffer CreateStagingBuffer() const;
    };
} // namespace Engine

#endif // RENDER_MEMORY_TEXTURE_INCLUDED
