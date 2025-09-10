#ifndef RENDER_MEMORY_RENDERTARGETTEXTURE_INCLUDED
#define RENDER_MEMORY_RENDERTARGETTEXTURE_INCLUDED

#include "Texture.h"

namespace Engine {

    /**
     * @brief A texture that can be rendered to.
     */
    class RenderTargetTexture : public Texture {
    public:
        struct RenderTargetTextureDesc {

#define COPY_ENUM_VALUE(x) x = (int)ImageUtils::ImageFormat::x
            enum class RTTFormat {
                COPY_ENUM_VALUE(R8G8B8A8SNorm),
                COPY_ENUM_VALUE(R8G8B8A8UNorm),
                COPY_ENUM_VALUE(R11G11B10UFloat),
                COPY_ENUM_VALUE(R32G32B32A32SFloat),
                COPY_ENUM_VALUE(D32SFLOAT),
            };
#undef COPY_ENUM_VALUE

            uint32_t dimensions;
            uint32_t width, height, depth;
            uint32_t mipmap_levels;
            uint32_t array_layers;
            RTTFormat format;

            // Multisample level. If set to a value higher than One,
            // an additional texture containing multisamples will be
            // created.
            // XXX: Not tested.
            uint8_t multisample;
            bool is_cube_map;
        };

        RenderTargetTexture(
            RenderSystem & system, 
            RenderTargetTextureDesc texture, 
            SamplerDesc sampler,
            const std::string & name = ""
        );
    };
}

#endif // RENDER_MEMORY_RENDERTARGETTEXTURE_INCLUDED
