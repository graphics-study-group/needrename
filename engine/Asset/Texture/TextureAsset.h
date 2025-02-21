#ifndef ASSET_TEXTURE_TEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_TEXTUREASSET_INCLUDED

#include <string>
#include <Asset/Asset.h>
#include <Reflection/macros.h>

namespace Engine
{
    class REFL_SER_CLASS(REFL_WHITELIST) TextureAsset : public Asset
    {
        REFL_SER_BODY(TextureAsset)
    public:
        REFL_ENABLE TextureAsset() = default;
        virtual ~TextureAsset() = default;

        REFL_SER_ENABLE std::string m_name {};
    };
}

#endif // ASSET_TEXTURE_TEXTUREASSET_INCLUDED
