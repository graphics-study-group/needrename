#ifndef ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED

#include "TextureAsset.h"
#include <Reflection/macros.h>
#include <Render/ImageUtils.h>
#include <vector>

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
        REFL_ENABLE Image2DTextureAsset() = default;
        virtual ~Image2DTextureAsset() = default;

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
        std::vector<std::byte> m_data{};
    };
} // namespace Engine

#endif // ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
