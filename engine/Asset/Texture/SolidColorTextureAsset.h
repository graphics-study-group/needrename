#ifndef ASSET_TEXTURE_SOLIDCOLORTEXTUREASSET_INCLUDED
#define ASSET_TEXTURE_SOLIDCOLORTEXTUREASSET_INCLUDED

#include "TextureAsset.h"
#include <Reflection/macros.h>
#include <Reflection/serialization_glm.h>
#include <glm.hpp>

namespace Engine {
    class REFL_SER_CLASS(REFL_WHITELIST) SolidColorTextureAsset : public TextureAsset {
        REFL_SER_BODY(SolidColorTextureAsset)
    public:
        REFL_ENABLE SolidColorTextureAsset() = default;
        virtual ~SolidColorTextureAsset() = default;

        REFL_SER_ENABLE glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};
    };
} // namespace Engine

#endif // ASSET_TEXTURE_SOLIDCOLORTEXTUREASSET_INCLUDED
