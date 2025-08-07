#ifndef RENDER_MEMORY_TEXTURESLICE_INCLUDED
#define RENDER_MEMORY_TEXTURESLICE_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class Texture;
    class SampledTexture;
    class RenderSystem;

    struct TextureSlice {
        uint32_t mipmap_base;
        uint32_t mipmap_size;
        uint32_t array_base;
        uint32_t array_size;
    };

    /**
     * @brief A slice (mipmap levels and array layers) of a given texture.
     * Wrapper around vk::ImageView.
     * 
     * XXX: We need to think of a better way of distinguishing sampled and non-sampled textures.
     */
    class SlicedTextureView {
        RenderSystem & m_system;
        const Texture & m_texture;

        TextureSlice m_slice;
        vk::UniqueImageView m_view;
    
    public:
        SlicedTextureView(RenderSystem & system, const Texture & texture, TextureSlice slice);

        const TextureSlice & GetSlice() const noexcept;

        const Texture & GetTexture() const noexcept;

        vk::ImageView GetImageView() const noexcept;
    };

    class SlicedSampledTextureView : public SlicedTextureView 
    {
    public:
        SlicedSampledTextureView(RenderSystem & system, SampledTexture & texture, TextureSlice slice);
    };
}

#endif // RENDER_MEMORY_TEXTURESLICE_INCLUDED
