#ifndef ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED
#define ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED

#include "Asset/Texture/TextureAsset.h"
#include "Reflection/macros.h"
#include <vector>

namespace Engine {
    namespace detail::texture_import {
        struct Access;
    }

    /**
     * @brief An asset for a cubemap
     */
    class REFL_SER_CLASS(REFL_WHITELIST) ImageCubemapAsset : public TextureAsset {
        REFL_SER_BODY(ImageCubemapAsset)
    public:
        REFL_ENABLE ImageCubemapAsset() = default;
        virtual ~ImageCubemapAsset() = default;

        /**
         * @brief Get pixel data of the cubemap.
         *
         * @return Data of the cubemap.
         * Its layout conforms to the Vulkan spec: six layers of 2D images of
         * the same size, in the order of +X, -X, +Y, -Y, +Z, -Z.
         */
        const std::byte *GetPixelData() const;
        size_t GetPixelDataSize() const;

        /// @brief Width of each face of the cubemap.
        REFL_SER_ENABLE int m_width{};
        /// @brief Height of each face of the cubemap.
        REFL_SER_ENABLE int m_height{};
        /// @brief Channel count of each face of the cubemap.
        REFL_SER_ENABLE int m_channel{};

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

    protected:
        friend struct detail::texture_import::Access;
        std::vector<std::byte> m_data{};
    };
} // namespace Engine

#endif // ASSET_TEXTURE_IMAGECUBEMAPASSET_INCLUDED
