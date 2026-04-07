#ifndef RENDER_MEMORY_RENDERTARGETTEXTURE
#define RENDER_MEMORY_RENDERTARGETTEXTURE

#include "Texture.h"

namespace Engine {

    /**
     * @brief A texture that can be rendered to.
     */
    class RenderTargetTexture : public Texture {
    public:
        /// @brief Description of a render target texture.
        /// @see `Engine::ImageUtils::TextureDesc`
        struct RenderTargetTextureDesc {

#define COPY_ENUM_VALUE(x) x = (int)ImageUtils::ImageFormat::x
            /**
             * Formats available for Render Target Textures.
             *
             * @see Engine::ImageUtils::ImageFormat
             */
            enum class RTTFormat {
                /// @copydoc Engine::ImageUtils::ImageFormat::R8G8B8A8SNorm
                COPY_ENUM_VALUE(R8G8B8A8SNorm),
                /// @copydoc Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm
                COPY_ENUM_VALUE(R8G8B8A8UNorm),
                /// @copydoc Engine::ImageUtils::ImageFormat::R11G11B10UFloat
                COPY_ENUM_VALUE(R11G11B10UFloat),
                /// @copydoc Engine::ImageUtils::ImageFormat::R32G32B32A32SFloat
                COPY_ENUM_VALUE(R32G32B32A32SFloat),
                /// @copydoc Engine::ImageUtils::ImageFormat::D32SFLOAT
                COPY_ENUM_VALUE(D32SFLOAT),
            };
#undef COPY_ENUM_VALUE

            /// An integer between 1 and 3 for dimension.
            uint32_t dimensions;
            /// Integers at least 1 for width in pixel.
            uint32_t width;
            /// Integers at least 1 for height in pixel.
            uint32_t height;
            /// Integers at least 1 for depth in pixel.
            uint32_t depth;
            /// Mipmap levels of the texture.
            uint32_t mipmap_levels;
            /// Array layers of the texture.
            uint32_t array_layers;
            /// Format of this image
            RTTFormat format;
            /**
             * @brief Sample count of this render target texture.
             *
             * Use values other than 1 enables multisample for the texture.
             * If a multisampled texture is used as an attachment, the graphics
             * pipeline might have multisampling enabled.
             */
            uint8_t multisample{1};
            /// Whether the texture is a cubemap.
            bool is_cube_map{false};
        };
        using RTTFormat = RenderTargetTextureDesc::RTTFormat;

    protected:
        RenderTargetTexture(
            RenderSystem &system, TextureDesc texture, SamplerDesc sampler, const std::string &name = ""
        );

        bool support_random_access{false}, support_atomic_access{false};

    public:
        /**
         * @brief Create a render target texture by description.
         *
         * @deprecated Using the unique_ptr variant is recommended.
         */
        static RenderTargetTexture Create(
            RenderSystem &system, RenderTargetTextureDesc texture, SamplerDesc sampler, const std::string &name = ""
        );
        /**
         * @brief Create a render target texture by description.
         */
        static std::unique_ptr<RenderTargetTexture> CreateUnique(
            RenderSystem &system, RenderTargetTextureDesc texture, SamplerDesc sampler, const std::string &name = ""
        );

        bool SupportRandomAccess() const noexcept override;
        bool SupportAtomicOperation() const noexcept override;
    };
} // namespace Engine

#endif // RENDER_MEMORY_RENDERTARGETTEXTURE
