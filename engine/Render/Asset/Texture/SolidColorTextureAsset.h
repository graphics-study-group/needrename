#ifndef RENDER_ASSET_TEXTURE_SOLIDCOLORTEXTUREASSET_INCLUDED
#define RENDER_ASSET_TEXTURE_SOLIDCOLORTEXTUREASSET_INCLUDED

#include "Render/render_export.h"
#include "TextureAsset.h"
#include <AnnoRefl/macros.h>
#include <AnnoRefl/serialization_glm.h>
#include <glm.hpp>

namespace Engine {
    class RENDER_API REFL_SER_CLASS(REFL_WHITELIST) SolidColorTextureAsset : public TextureAsset {
        REFL_SER_BODY_OVERRIDE(SolidColorTextureAsset)
    public:
        REFL_ENABLE SolidColorTextureAsset() = default;
        virtual ~SolidColorTextureAsset() = default;

        REFL_SER_ENABLE glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};
    };
} // namespace Engine

#endif // RENDER_ASSET_TEXTURE_SOLIDCOLORTEXTUREASSET_INCLUDED
