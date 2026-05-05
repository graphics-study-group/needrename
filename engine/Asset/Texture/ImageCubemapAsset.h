#ifndef ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED
#define ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED

#include "Asset/Texture/TextureAsset.h"
#include "Reflection/macros.h"
#include <Render/ImageUtils.h>
#include <vector>

struct ktxTexture2;

namespace Engine {
    namespace detail::texture_import {
        struct Access;
    }

    /**
     * @brief An asset for a cubemap
     */
    class REFL_SER_CLASS(REFL_WHITELIST) ImageCubemapAsset : public TextureAsset {
        REFL_SER_BODY(ImageCubemapAsset)
    public:
        REFL_ENABLE ImageCubemapAsset();
        virtual ~ImageCubemapAsset() override;

        /**
         * @brief Get pixel data of the cubemap.
         *
         * @return Data of the cubemap.
         * Its layout conforms to the Vulkan spec: six layers of 2D images of
         * the same size, in the order of +X, -X, +Y, -Y, +Z, -Z.
         */
        const std::byte *GetPixelData() const;
        size_t GetPixelDataSize() const;

        /// @brief Width of each face of the cubemap.
        REFL_SER_ENABLE int m_width{};
        /// @brief Height of each face of the cubemap.
        REFL_SER_ENABLE int m_height{};
        /// @brief Channel count of each face of the cubemap.
        REFL_SER_ENABLE int m_channel{};

        /**
         * @brief Expected memory format of the cubemap.
         *
         * This member affects only how the image should be represented on the
         * GPU memory. It does not reflect its actual format on the desk.
         */
        REFL_SER_ENABLE ImageUtils::ImageFormat m_format{};

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

    protected:
        friend struct detail::texture_import::Access;
        /**
         * @brief Set the decoded pixel data of the cubemap.
         *
         * The data should be the image pixel data decoded from an image file, without any header, metadata or compression.
         */
        void SetDecodedData(
            int width, int height, int channel, std::vector<std::byte> data, ImageUtils::ImageFormat format
        );

    private:
        ktxTexture2 *m_texture{};

        /**
         * @brief Reset the texture with a new ktxTexture2 object. The old texture will be destroyed.
         */
        void ResetTexture(ktxTexture2 *texture);
    };
} // namespace Engine

#endif // ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED
