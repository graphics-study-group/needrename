#ifndef RENDER_ASSET_TEXTURE_TEXTUREASSET_INCLUDED
#define RENDER_ASSET_TEXTURE_TEXTUREASSET_INCLUDED

#include "Render/render_export.h"
#include <AnnoRefl/macros.h>
#include <Asset/Asset.h>
#include <string>

namespace Engine {
    /// @brief Base class of all texture assets.
    class RENDER_API REFL_SER_CLASS(REFL_WHITELIST) TextureAsset : public Asset {
        REFL_SER_BODY_OVERRIDE(TextureAsset)
    public:
        REFL_ENABLE TextureAsset() = default;
        virtual ~TextureAsset() = default;

        REFL_SER_ENABLE std::string m_name{};
    };
} // namespace Engine

#endif // RENDER_ASSET_TEXTURE_TEXTUREASSET_INCLUDED
