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
            uint8_t multisample {1};
            bool is_cube_map {false};
        };
        using RTTFormat = RenderTargetTextureDesc::RTTFormat;
    
    protected:
        RenderTargetTexture(
            RenderSystem & system,
            TextureDesc texture,
            SamplerDesc sampler,
            const std::string & name = ""
        );

        bool support_random_access{false}, support_atomic_access{false};
    
    public:
        static RenderTargetTexture Create(
            RenderSystem & system, 
            RenderTargetTextureDesc texture, 
            SamplerDesc sampler,
            const std::string & name = ""
        );
        static std::unique_ptr <RenderTargetTexture> CreateUnique(
            RenderSystem & system, 
            RenderTargetTextureDesc texture, 
            SamplerDesc sampler,
            const std::string & name = ""
        );

        bool SupportRandomAccess() const noexcept override;
        bool SupportAtomicOperation() const noexcept override;
    };
}

#endif // RENDER_MEMORY_RENDERTARGETTEXTURE_INCLUDED
