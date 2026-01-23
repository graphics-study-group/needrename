#ifndef ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED
#define ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED

#include "Asset/Texture/TextureAsset.h"
#include "Reflection/macros.h"

namespace Engine {
    class REFL_SER_CLASS(REFL_WHITELIST) ImageCubemapAsset : public TextureAsset {
        REFL_SER_BODY(ImageCubemapAsset)
    public:
        REFL_ENABLE ImageCubemapAsset() = default;
        virtual ~ImageCubemapAsset() = default;

        /**
         * @brief Load a cubemap from a single 2:1 Equirectangular Projection image file.
         * @param paths Path to the image file.
         * @param width Desired width of the cubemap faces.
         * @param height Desired height of the cubemap faces.
         */
        void LoadFromFile(const std::filesystem::path &paths, int width, int height);
        /**
         * @brief Load a cubemap from six separate image files of the same size and format.
         * Accepts gamma corrected sRGB SDR image of only RGB channels.
         * 
         * @param paths Paths to six images representing each face, in the order of:
         * X+, X-, Y+, Y-, Z+, Z-. Physical meaning of the orientation (e.g. front or back)
         * is determined in the shader code which samples the image.
         */
        void LoadFromFile(const std::array <std::filesystem::path, 6> & paths);
        const std::byte *GetPixelData() const;
        size_t GetPixelDataSize() const;

        REFL_SER_ENABLE int m_width{};
        REFL_SER_ENABLE int m_height{};
        REFL_SER_ENABLE int m_channel{};

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

    protected:
        std::vector<std::byte> m_data{};
    };
}


#endif // ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED
