#ifndef ASSET_LOADER_TEXTUREIMPORTUTILS_INCLUDED
#define ASSET_LOADER_TEXTUREIMPORTUTILS_INCLUDED

#include <Render/ImageUtils.h>

#include <array>
#include <cstddef>
#include <filesystem>

namespace Engine {
    class Image2DTextureAsset;
    class ImageCubemapAsset;
}

namespace Engine::detail::texture_import {
    void LoadImage2DTextureAssetFromFile(
        Engine::Image2DTextureAsset &asset,
        const std::filesystem::path &path,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );

    void LoadImage2DTextureAssetFromMemory(
        Engine::Image2DTextureAsset &asset,
        const std::byte *bytes,
        size_t size,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );

    void LoadImageCubemapAssetFromEquirectangularFile(
        Engine::ImageCubemapAsset &asset,
        const std::filesystem::path &path,
        int width,
        int height
    );

    void LoadImageCubemapAssetFromSixFiles(
        Engine::ImageCubemapAsset &asset,
        const std::array<std::filesystem::path, 6> &paths
    );

    void LoadImageCubemapAssetFromEquirectangularMemory(
        Engine::ImageCubemapAsset &asset,
        const std::byte *bytes,
        size_t size,
        int width,
        int height
    );

    void LoadImageCubemapAssetFromSixMemory(
        Engine::ImageCubemapAsset &asset,
        const std::array<const std::byte *, 6> &bytes,
        const std::array<size_t, 6> &sizes
    );
} // namespace Engine::detail::texture_import

#endif // ASSET_LOADER_TEXTUREIMPORTUTILS_INCLUDED
