#ifndef ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED

#include "TextureAsset.h"
#include <Render/ImageUtils.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class REFL_SER_CLASS(REFL_WHITELIST) Image2DTextureAsset : public TextureAsset
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE Image2DTextureAsset() = default;
        virtual ~Image2DTextureAsset() = default;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;
        virtual void Unload() override;

        REFL_SER_ENABLE int m_width{};
        REFL_SER_ENABLE int m_height{};
        REFL_SER_ENABLE int m_channel{};
        REFL_SER_ENABLE ImageUtils::ImageFormat m_format{};
        REFL_SER_ENABLE uint32_t m_mip_level{};

        void LoadFromMemory(const std::byte *data, size_t size);
        void LoadFromFile(const std::filesystem::path &path);
        std::byte *GetPixelData();
        size_t GetPixelDataSize() const;

    protected:
        std::vector<std::byte> m_data{};
    };
}

#endif // ASSET_TEXTURE_IMAGE2DTEXTUREASSET_INCLUDED
