#ifndef ASSET_LOADER_TEXTUREIMPORTUTILS_INCLUDED
#define ASSET_LOADER_TEXTUREIMPORTUTILS_INCLUDED

#include <Render/ImageUtils.h>

#include <array>
#include <cstddef>
#include <filesystem>

namespace Engine {
    class Image2DTextureAsset;
    class ImageCubemapAsset;
} // namespace Engine

namespace Engine::detail::texture_import {
    /**
     * @brief Load a 2D texture asset from an image file.
     *
     * @param asset The texture asset to load into.
     * @param path Path to the image file.
     * @param format Expected memory format of the texture. It affects only how the image should be represented on the GPU memory, and does not reflect its actual format on the disk.
     */
    void LoadImage2DTextureAssetFromFile(
        Engine::Image2DTextureAsset &asset,
        const std::filesystem::path &path,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );

    /**
     * @brief Load a 2D texture asset from memory.
     *
     * @param asset The texture asset to load into.
     * @param bytes Pointer to the image data in memory. The data should be the raw image file data, including any header, metadata or compression.
     * @param size Size of the image data in bytes.
     * @param format Expected memory format of the texture. It affects only how the image should be represented on the GPU memory, and does not reflect its actual format on the disk.
     */
    void LoadImage2DTextureAssetFromMemory(
        Engine::Image2DTextureAsset &asset,
        const std::byte *bytes,
        size_t size,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );

    /**
     * @brief Load a cubemap texture asset from an equirectangular image file.
     *
     * The image will be converted to cubemap layout on the CPU.
     *
     * @param asset The cubemap texture asset to load into.
     * @param path Path to the equirectangular image file.
     * @param width Width of each face of the cubemap.
     * @param height Height of each face of the cubemap.
     * @param format Expected memory format of the cubemap. It affects only how the image should be represented on the GPU memory, and does not reflect its actual format on the disk.
     */
    void LoadImageCubemapAssetFromEquirectangularFile(
        Engine::ImageCubemapAsset &asset,
        const std::filesystem::path &path,
        int width,
        int height,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );

    /**
     * @brief Load a cubemap texture asset from six image files.
     *
     * The six images should have the same dimensions and channel count. They will be combined into a cubemap layout on the CPU.
     * The expected order of the six images in the array is +X, -X, +Y, -Y, +Z, -Z.
     *
     * @param asset The cubemap texture asset to load into.
     * @param paths Paths to the six image files.
     * @param format Expected memory format of the cubemap. It affects only how the image should be represented on the GPU memory, and does not reflect its actual format on the disk.
     */
    void LoadImageCubemapAssetFromSixFiles(
        Engine::ImageCubemapAsset &asset,
        const std::array<std::filesystem::path, 6> &paths,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );

    /**
     * @brief Load a cubemap texture asset from six images in memory.
     *
     * The six images should have the same dimensions and channel count. They will be combined into a cubemap layout on the CPU.
     * The expected order of the six images in the array is +X, -X, +Y, -Y, +Z, -Z.
     *
     * @param asset The cubemap texture asset to load into.
     * @param bytes Pointers to the six images in memory.
     * @param sizes Sizes of the six images in bytes.
     * @param format Expected memory format of the cubemap. It affects only how the image should be represented on the GPU memory, and does not reflect its actual format on the disk.
     */
    void LoadImageCubemapAssetFromEquirectangularMemory(
        Engine::ImageCubemapAsset &asset,
        const std::byte *bytes,
        size_t size,
        int width,
        int height,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );

    /**
     * @brief Load a cubemap texture asset from six images in memory.
     *
     * The six images should have the same dimensions and channel count. They will be combined into a cubemap layout on the CPU.
     * The expected order of the six images in the array is +X, -X, +Y, -Y, +Z, -Z.
     *
     * @param asset The cubemap texture asset to load into.
     * @param bytes Pointers to the six images in memory.
     * @param sizes Sizes of the six images in bytes.
     * @param format Expected memory format of the cubemap. It affects only how the image should be represented on the GPU memory, and does not reflect its actual format on the disk.
     */
    void LoadImageCubemapAssetFromSixMemory(
        Engine::ImageCubemapAsset &asset,
        const std::array<const std::byte *, 6> &bytes,
        const std::array<size_t, 6> &sizes,
        ImageUtils::ImageFormat format = ImageUtils::ImageFormat::R8G8B8A8SRGB
    );
} // namespace Engine::detail::texture_import

#endif // ASSET_LOADER_TEXTUREIMPORTUTILS_INCLUDED
