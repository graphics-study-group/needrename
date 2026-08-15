#ifndef RENDER_ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
#define RENDER_ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED

#include "Render/render_export.h"
#include "TextureAsset.h"
#include <Rhi/Texture/ImageTexture.h>
#include <Rhi/Texture/ImageUtils.h>

#include <AnnoRefl/macros.h>

#include <memory>
#include <vector>

struct ktxTexture2;

namespace Engine {
    namespace detail::texture_import {
        struct Access;
    }

    /**
     * @brief An asset for a 2D texture.
     */
    class RENDER_API REFL_SER_CLASS(REFL_WHITELIST) Image2DTextureAsset : public TextureAsset {
        REFL_SER_BODY_OVERRIDE(Image2DTextureAsset)
    public:
        REFL_ENABLE Image2DTextureAsset();
        virtual ~Image2DTextureAsset() override;

        virtual void save_asset_to_archive(AnnoRefl::Archive &archive) const override;
        virtual void load_asset_from_archive(AnnoRefl::Archive &archive) override;

        /// @brief Width in pixel of the texture.
        REFL_SER_ENABLE int m_width{};
        /// @brief Height in pixel of the texture.
        REFL_SER_ENABLE int m_height{};
        /// @brief Channels of the texture.
        REFL_SER_ENABLE int m_channel{};

        /**
         * @brief Expected memory format of the texture.
         *
         * This member affects only how the image should be represented on the
         * GPU memory. It does not reflect its actual format on the desk.
         */
        REFL_SER_ENABLE Rhi::ImageFormat m_format{};

        /***
         * @brief Expected mipmap level of the texture.
         *
         * Unused.
         */
        REFL_SER_ENABLE unsigned m_mip_level{};

        /// @brief Get pixel data.
        const std::byte *GetPixelData() const;
        /// @brief Get the size of all pixel data
        size_t GetPixelDataSize() const;

        /**
         * @brief Create an image texture from this asset.
         *
         * Width, height, format and mip levels are read from the asset.
         * The created texture's content is not uploaded until submitted.
         *
         * @param device_context Device context used to create the texture.
         * @return The created image texture.
         */
        std::unique_ptr<Rhi::ImageTexture> CreateImageTexture(Rhi::DeviceContext &device_context) const;

    protected:
        friend struct detail::texture_import::Access;
        /**
         * @brief Set the decoded pixel data of the texture.
         *
         * The data should be the image pixel data decoded from an image file, without any header, metadata or compression.
         */
        void SetDecodedData(
            int width, int height, int channel, std::vector<std::byte> data, Rhi::ImageFormat format, unsigned mip_level
        );

    private:
        ktxTexture2 *m_texture{};

        /**
         * @brief Reset the texture with a new ktxTexture2 object. The old texture will be destroyed.
         */
        void ResetTexture(ktxTexture2 *texture);
    };
} // namespace Engine

#endif // RENDER_ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
