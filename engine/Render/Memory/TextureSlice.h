#ifndef RENDER_MEMORY_TEXTURESLICE_INCLUDED
#define RENDER_MEMORY_TEXTURESLICE_INCLUDED

#include <memory>

namespace vk {
    class ImageView;
}

namespace Engine {
    class Texture;
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
     */
    class SlicedTextureView {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        SlicedTextureView(RenderSystem &system, const Texture &texture, TextureSlice slice);
        ~SlicedTextureView();

        const TextureSlice &GetSlice() const noexcept;

        const Texture &GetTexture() const noexcept;

        vk::ImageView GetImageView() const noexcept;
    };
} // namespace Engine

#endif // RENDER_MEMORY_TEXTURESLICE_INCLUDED
