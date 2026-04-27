#ifndef ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED

#include "TextureAsset.h"
#include <Reflection/macros.h>
#include <Render/ImageUtils.h>
#include <vector>

namespace Engine {
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

        /**
         * @brief Load the asset from a disk file.
         *
         * Currently implemented via the `stbi_image` library.
         * Reads only the the first mipmap level.
         * Does not support compressed format (e.g. ATSC). Such textures will
         * be uncompressed first via the `stbi_image` library.
         *
         * @param path Path of the image.
         */
        void LoadFromFile(const std::filesystem::path &path);

        /**
         * @brief Load the asset from encoded image bytes in memory.
         *
         * The input should be encoded image content such as PNG/JPEG bytes.
         *
         * @param bytes Pointer to encoded image bytes.
         * @param size Byte size of encoded image content.
         */
        void LoadFromMemory(const std::byte *bytes, size_t size);

        /// @brief Get pixel data.
        const std::byte *GetPixelData() const;
        /// @brief Get the size of all pixel data
        size_t GetPixelDataSize() const;

    protected:
        std::vector<std::byte> m_data{};
    };
} // namespace Engine

#endif // ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
