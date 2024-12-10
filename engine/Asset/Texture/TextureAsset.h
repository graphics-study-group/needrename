#ifndef ASSET_TEXTURE_TEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_TEXTUREASSET_INCLUDED

#include <vector>
#include <Asset/Asset.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class TextureAsset : public Asset
    {
    public:
        TextureAsset() = default;
        virtual ~TextureAsset() = default;

    public:
        std::string m_name;
        
        std::vector<std::byte> m_data;

        virtual void save_asset_to_archive(Serialization::Archive& archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive& archive) override;
    };
}

#endif // ASSET_TEXTURE_TEXTUREASSET_INCLUDED
