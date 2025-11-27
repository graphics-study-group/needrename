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

    protected:
        std::vector<std::byte> m_data{};
    };
}


#endif // ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED
