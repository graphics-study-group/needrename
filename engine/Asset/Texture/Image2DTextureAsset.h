#ifndef ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED

#include "TextureAsset.h"
#include <Reflection/macros.h>
#include <Render/ImageUtils.h>
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
    class REFL_SER_CLASS(REFL_WHITELIST) Image2DTextureAsset : public TextureAsset {
        REFL_SER_BODY(Image2DTextureAsset)
    public:
        REFL_ENABLE Image2DTextureAsset();
        virtual ~Image2DTextureAsset() override;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

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
        REFL_SER_ENABLE ImageUtils::ImageFormat m_format{};

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

    protected:
        friend struct detail::texture_import::Access;
        void SetDecodedData(
            int width,
            int height,
            int channel,
            std::vector<std::byte> data,
            ImageUtils::ImageFormat format,
            unsigned mip_level
        );

    private:
        ktxTexture2 *m_texture{};

        /**
         * @brief Reset the texture with a new ktxTexture2 object. The old texture will be destroyed.
         */
        void ResetTexture(ktxTexture2 *texture);
    };
} // namespace Engine

#endif // ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
