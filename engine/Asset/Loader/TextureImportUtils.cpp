#include "TextureImportUtils.h"

#include "Asset/Texture/Image2DTextureAsset.h"
#include "Asset/Texture/ImageCubemapAsset.h"

#include <glm.hpp>
#include <numbers>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
    struct DecodedImage2D {
        int width{};
        int height{};
        int channel{};
        std::vector<std::byte> data{};
    };

    DecodedImage2D DecodeImage2DFromMemory(const std::byte *bytes, size_t size) {
        int width = 0;
        int height = 0;
        int channel = 0;
        stbi_uc *raw_image_data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc *>(bytes), static_cast<int>(size), &width, &height, &channel, 4
        );
        assert(raw_image_data);

        DecodedImage2D image{};
        image.width = width;
        image.height = height;
        image.channel = 4;
        image.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        std::memcpy(image.data.data(), raw_image_data, image.data.size());
        stbi_image_free(raw_image_data);
        return image;
    }

    void SampleBilinear(const std::byte *src, int w, int h, int c, float x, float y, std::byte *out) {
        int x0 = static_cast<int>(std::floor(x));
        int y0 = static_cast<int>(std::floor(y));
        int x1 = (x0 + 1) % w;
        int y1 = std::min(y0 + 1, h - 1);

        float tx = x - x0;
        float ty = y - y0;

        for (int k = 0; k < c; ++k) {
            float c00 = static_cast<float>(src[(y0 * w + x0) * c + k]);
            float c10 = static_cast<float>(src[(y0 * w + x1) * c + k]);
            float c01 = static_cast<float>(src[(y1 * w + x0) * c + k]);
            float c11 = static_cast<float>(src[(y1 * w + x1) * c + k]);

            float c0 = glm::mix(c00, c10, tx);
            float c1 = glm::mix(c01, c11, tx);

            out[k] = static_cast<std::byte>(glm::mix(c0, c1, ty));
        }
    }

    std::vector<std::byte> ConvertEquirectToCubemap(
        const std::byte *src, int src_w, int src_h, int channels, int face_size
    ) {
        std::vector<std::byte> output{};
        output.resize(
            static_cast<size_t>(face_size) * static_cast<size_t>(face_size) * static_cast<size_t>(channels) * 6
        );

        for (int face = 0; face < 6; ++face) {
            std::vector<std::byte> dst(static_cast<size_t>(face_size) * static_cast<size_t>(face_size) * channels);

            for (int y = 0; y < face_size; ++y) {
                for (int x = 0; x < face_size; ++x) {
                    float u = 2.0f * (x + 0.5f) / face_size - 1.0f;
                    float v = 2.0f * (y + 0.5f) / face_size - 1.0f;

                    glm::vec3 dir;
                    switch (face) {
                    case 0:
                        dir = {1, -v, -u};
                        break;
                    case 1:
                        dir = {-1, -v, u};
                        break;
                    case 2:
                        dir = {u, 1, v};
                        break;
                    case 3:
                        dir = {u, -1, -v};
                        break;
                    case 4:
                        dir = {u, -v, 1};
                        break;
                    case 5:
                        dir = {-u, -v, -1};
                        break;
                    default:
                        assert(false);
                        dir = {0, 0, 1};
                        break;
                    }

                    dir = glm::normalize(dir);
                    float lon = std::atan2(dir.y, dir.x);
                    float lat = -std::asin(dir.z);

                    float src_x = (lon + std::numbers::pi_v<float>) / (2.0f * std::numbers::pi_v<float>)*src_w;
                    float src_y = (std::numbers::pi_v<float> / 2.0f + lat) / std::numbers::pi_v<float> * src_h;

                    std::byte *pixel = &dst[(y * face_size + x) * channels];
                    SampleBilinear(src, src_w, src_h, channels, src_x, src_y, pixel);
                }
            }

            const size_t face_data_size =
                static_cast<size_t>(face_size) * static_cast<size_t>(face_size) * static_cast<size_t>(channels);
            std::memcpy(output.data() + static_cast<size_t>(face) * face_data_size, dst.data(), face_data_size);
        }

        return output;
    }
} // namespace

namespace Engine::detail::texture_import {
    struct Access {
        static void Set2DTextureName(Image2DTextureAsset &asset, const std::string &name) {
            asset.m_name = name;
        }

        static void Set2DTextureDecodedData(
            Image2DTextureAsset &asset,
            int width,
            int height,
            int channel,
            std::vector<std::byte> data,
            ImageUtils::ImageFormat format,
            unsigned mip_level
        ) {
            asset.m_width = width;
            asset.m_height = height;
            asset.m_channel = channel;
            asset.m_data = std::move(data);
            asset.m_format = format;
            asset.m_mip_level = mip_level;
        }

        static void SetCubemapDecodedData(
            ImageCubemapAsset &asset,
            int width,
            int height,
            int channel,
            std::vector<std::byte> data,
            ImageUtils::ImageFormat format
        ) {
            asset.m_width = width;
            asset.m_height = height;
            asset.m_channel = channel;
            asset.m_data = std::move(data);
            asset.m_format = format;
        }
    };

    void LoadImage2DTextureAssetFromFile(
        Image2DTextureAsset &asset, const std::filesystem::path &path, ImageUtils::ImageFormat format
    ) {
        int width = 0;
        int height = 0;
        int channel = 0;
        stbi_uc *raw_image_data = stbi_load(path.string().c_str(), &width, &height, &channel, 4);
        assert(raw_image_data);

        std::vector<std::byte> data(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        std::memcpy(data.data(), raw_image_data, data.size());
        stbi_image_free(raw_image_data);

        Access::Set2DTextureName(asset, path.stem().string());
        Access::Set2DTextureDecodedData(asset, width, height, 4, std::move(data), format, 1);
    }

    void LoadImage2DTextureAssetFromMemory(
        Image2DTextureAsset &asset, const std::byte *bytes, size_t size, ImageUtils::ImageFormat format
    ) {
        DecodedImage2D image = DecodeImage2DFromMemory(bytes, size);
        Access::Set2DTextureDecodedData(
            asset, image.width, image.height, image.channel, std::move(image.data), format, 1
        );
    }

    void LoadImageCubemapAssetFromEquirectangularFile(
        ImageCubemapAsset &asset,
        const std::filesystem::path &path,
        int width,
        int height,
        ImageUtils::ImageFormat format
    ) {
        int src_w = 0;
        int src_h = 0;
        int channels = 0;
        stbi_uc *raw_image_data = stbi_load(path.string().c_str(), &src_w, &src_h, &channels, 4);
        assert(raw_image_data);

        std::vector<std::byte> data =
            ConvertEquirectToCubemap(reinterpret_cast<const std::byte *>(raw_image_data), src_w, src_h, 4, width);
        stbi_image_free(raw_image_data);

        Access::SetCubemapDecodedData(asset, width, height, 4, std::move(data), format);
    }

    void LoadImageCubemapAssetFromSixFiles(
        ImageCubemapAsset &asset, const std::array<std::filesystem::path, 6> &paths, ImageUtils::ImageFormat format
    ) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *first_image = stbi_load(paths[0].string().c_str(), &width, &height, &channels, 4);
        assert(first_image);

        const size_t image_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        std::vector<std::byte> data(image_size * 6);
        std::memcpy(data.data(), first_image, image_size);
        stbi_image_free(first_image);

        for (int i = 1; i < 6; i++) {
            int next_width = 0;
            int next_height = 0;
            int next_channels = 0;
            stbi_uc *image = stbi_load(paths[i].string().c_str(), &next_width, &next_height, &next_channels, 4);
            assert(image);
            assert(next_width == width && next_height == height && next_channels == channels);

            std::memcpy(data.data() + image_size * static_cast<size_t>(i), image, image_size);
            stbi_image_free(image);
        }

        Access::SetCubemapDecodedData(asset, width, height, 4, std::move(data), format);
    }

    void LoadImageCubemapAssetFromEquirectangularMemory(
        ImageCubemapAsset &asset,
        const std::byte *bytes,
        size_t size,
        int width,
        int height,
        ImageUtils::ImageFormat format
    ) {
        DecodedImage2D image = DecodeImage2DFromMemory(bytes, size);
        std::vector<std::byte> data =
            ConvertEquirectToCubemap(image.data.data(), image.width, image.height, image.channel, width);
        Access::SetCubemapDecodedData(asset, width, height, 4, std::move(data), format);
    }

    void LoadImageCubemapAssetFromSixMemory(
        ImageCubemapAsset &asset,
        const std::array<const std::byte *, 6> &bytes,
        const std::array<size_t, 6> &sizes,
        ImageUtils::ImageFormat format
    ) {
        std::array<DecodedImage2D, 6> images{};
        for (int i = 0; i < 6; ++i) {
            images[i] = DecodeImage2DFromMemory(bytes[i], sizes[i]);
        }

        const int width = images[0].width;
        const int height = images[0].height;
        const int channel = images[0].channel;

        for (int i = 1; i < 6; ++i) {
            assert(images[i].width == width);
            assert(images[i].height == height);
            assert(images[i].channel == channel);
        }

        const size_t image_size =
            static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channel);
        std::vector<std::byte> data(image_size * 6);
        for (int i = 0; i < 6; ++i) {
            std::memcpy(data.data() + image_size * static_cast<size_t>(i), images[i].data.data(), image_size);
        }

        Access::SetCubemapDecodedData(asset, width, height, channel, std::move(data), format);
    }
} // namespace Engine::detail::texture_import
