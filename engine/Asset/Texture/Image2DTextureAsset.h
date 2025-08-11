#ifndef ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED

#include "TextureAsset.h"
#include <Reflection/macros.h>
#include <Render/ImageUtils.h>

namespace Engine {
    class REFL_SER_CLASS(REFL_WHITELIST) Image2DTextureAsset : public TextureAsset {
        REFL_SER_BODY(Image2DTextureAsset)
    public:
        REFL_ENABLE Image2DTextureAsset() = default;
        virtual ~Image2DTextureAsset() = default;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        REFL_SER_ENABLE int m_width{};
        REFL_SER_ENABLE int m_height{};
        REFL_SER_ENABLE int m_channel{};
        REFL_SER_ENABLE ImageUtils::ImageFormat m_format{};
        REFL_SER_ENABLE unsigned m_mip_level{};

        void LoadFromFile(const std::filesystem::path &path);
        const std::byte *GetPixelData() const;
        size_t GetPixelDataSize() const;

    protected:
        std::vector<std::byte> m_data{};
    };
} // namespace Engine

#endif // ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
