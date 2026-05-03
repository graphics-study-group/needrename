#include "ImageCubemapAsset.h"

#include <Render/ImageUtilsFunc.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <array>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <glm.hpp>
#include <ktx.h>
#include <numbers>
#include <stb_image.h>

#include <memory>

#include <Reflection/serialization.h>

namespace {
    void sampleBilinear(const std::byte *src, int w, int h, int c, float x, float y, std::byte *out) {
        int x0 = (int)std::floor(x);
        int y0 = (int)std::floor(y);
        int x1 = (x0 + 1) % w;
        int y1 = std::min(y0 + 1, h - 1);

        float tx = x - x0;
        float ty = y - y0;

        for (int k = 0; k < c; ++k) {
            float c00 = (float)src[(y0 * w + x0) * c + k];
            float c10 = (float)src[(y0 * w + x1) * c + k];
            float c01 = (float)src[(y1 * w + x0) * c + k];
            float c11 = (float)src[(y1 * w + x1) * c + k];

            float c0 = glm::mix(c00, c10, tx);
            float c1 = glm::mix(c01, c11, tx);

            out[k] = (std::byte)glm::mix(c0, c1, ty);
        }
    }
    void convertEquirectToCubemap(
        const std::byte *src, int srcW, int srcH, int channels, int faceSize, std::vector<std::byte> &out
    ) {
        out.resize(faceSize * faceSize * channels * 6);
        for (int face = 0; face < 6; ++face) {
            std::vector<std::byte> dst(faceSize * faceSize * channels);

            for (int y = 0; y < faceSize; ++y) {
                for (int x = 0; x < faceSize; ++x) {

                    float u = 2.0f * (x + 0.5f) / faceSize - 1.0f;
                    float v = 2.0f * (y + 0.5f) / faceSize - 1.0f;

                    glm::vec3 dir;
                    switch (face) {
                    case 0:
                        dir = {1, -v, -u};
                        break; // +X
                    case 1:
                        dir = {-1, -v, u};
                        break; // -X
                    case 2:
                        dir = {u, 1, v};
                        break; // +Y
                    case 3:
                        dir = {u, -1, -v};
                        break; // -Y
                    case 4:
                        dir = {u, -v, 1};
                        break; // +Z
                    case 5:
                        dir = {-u, -v, -1};
                        break; // -Z
                    }

                    dir = glm::normalize(dir);
                    float lon = std::atan2(dir.y, dir.x);
                    float lat = -std::asin(dir.z);

                    float srcX = (lon + std::numbers::pi_v<float>) / (2.0f * std::numbers::pi_v<float>)*srcW;
                    float srcY = (std::numbers::pi_v<float> / 2.0f - lat) / std::numbers::pi_v<float> * srcH;

                    std::byte *pixel = &dst[(y * faceSize + x) * channels];

                    sampleBilinear(src, srcW, srcH, channels, srcX, srcY, pixel);
                }
            }
            std::memcpy(&out[face * faceSize * faceSize * channels], dst.data(), dst.size());
        }
    }
} // namespace

namespace Engine {
    void ImageCubemapAsset::LoadFromFile(const std::filesystem::path &paths, int width, int height) {
        int srcW, srcH, channels;
        auto raw_image_data = stbi_load(paths.string().c_str(), &srcW, &srcH, &channels, 4);
        assert(raw_image_data);
        m_width = width;
        m_height = height;
        m_channel = 4;
        convertEquirectToCubemap(reinterpret_cast<const std::byte *>(raw_image_data), srcW, srcH, 4, width, m_data);
        stbi_image_free(raw_image_data);
    }

    void ImageCubemapAsset::LoadFromFile(const std::array<std::filesystem::path, 6> &paths) {
        int width, height, channels;
        auto first_image = stbi_load(paths[0].string().c_str(), &width, &height, &channels, 4);
        assert(first_image);
        m_width = width;
        m_height = height;
        m_channel = 4;

        auto image_size = width * height * 4;
        m_data.resize(image_size * 6);
        std::memcpy(m_data.data(), first_image, image_size);
        stbi_image_free(first_image);

        for (int i = 1; i < 6; i++) {
            int nw, nh, nc;
            auto image = stbi_load(paths[i].string().c_str(), &nw, &nh, &nc, 4);
            assert(image);
            assert(nw == width && nh == height && nc == channels);

            std::memcpy(m_data.data() + image_size * i, image, image_size);
            stbi_image_free(image);
        }
    }
    const std::byte *ImageCubemapAsset::GetPixelData() const {
        return m_data.data();
    }
    size_t ImageCubemapAsset::GetPixelDataSize() const {
        return m_data.size();
    }

    void ImageCubemapAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".ktx2");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];

        ktxTextureCreateInfo create_info{};
        create_info.vkFormat =
            static_cast<ktx_uint32_t>(ImageUtils::GetVkFormat(ImageUtils::ImageFormat::R8G8B8A8SRGB));
        create_info.baseWidth = static_cast<ktx_uint32_t>(m_width);
        create_info.baseHeight = static_cast<ktx_uint32_t>(m_height);
        create_info.baseDepth = 1;
        create_info.numDimensions = 2;
        create_info.numLevels = 1;
        create_info.numLayers = 1;
        create_info.numFaces = 6;
        create_info.isArray = KTX_FALSE;
        create_info.generateMipmaps = KTX_FALSE;

        ktxTexture2 *texture = nullptr;
        const auto create_error = ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
        assert(create_error == KTX_SUCCESS && texture != nullptr);
        std::unique_ptr<ktxTexture2, void (*)(ktxTexture2 *)> texture_guard(texture, ktxTexture2_Destroy);

        const size_t face_data_size = m_data.size() / 6;
        for (int face = 0; face < 6; ++face) {
            const auto set_image_error = ktxTexture_SetImageFromMemory(
                ktxTexture(texture),
                0,
                0,
                static_cast<ktx_uint32_t>(face),
                reinterpret_cast<const ktx_uint8_t *>(m_data.data() + face * face_data_size),
                static_cast<ktx_size_t>(face_data_size)
            );
            assert(set_image_error == KTX_SUCCESS);
        }

        ktx_uint8_t *raw_ktx_data = nullptr;
        ktx_size_t raw_ktx_size = 0;
        const auto write_error = ktxTexture_WriteToMemory(ktxTexture(texture), &raw_ktx_data, &raw_ktx_size);
        assert(write_error == KTX_SUCCESS && raw_ktx_data != nullptr);

        data.resize(static_cast<size_t>(raw_ktx_size));
        std::memcpy(data.data(), raw_ktx_data, static_cast<size_t>(raw_ktx_size));
        std::free(raw_ktx_data);

        TextureAsset::save_asset_to_archive(archive);
    }

    void ImageCubemapAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &json = *archive.m_cursor;
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];

        ktxTexture2 *texture = nullptr;
        const auto create_error = ktxTexture2_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t *>(data.data()),
            static_cast<ktx_size_t>(data.size()),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &texture
        );
        assert(create_error == KTX_SUCCESS && texture != nullptr);
        std::unique_ptr<ktxTexture2, void (*)(ktxTexture2 *)> texture_guard(texture, ktxTexture2_Destroy);

        if (ktxTexture2_NeedsTranscoding(texture)) {
            auto transcode_error = ktxTexture2_TranscodeBasis(texture, KTX_TTF_RGBA32, 0);
            if (transcode_error != KTX_SUCCESS) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION, "Cubemap transcode failed (%s).", ktxErrorString(transcode_error)
                );
            }
            assert(transcode_error == KTX_SUCCESS);
        }

        assert(texture->numFaces == 6 && "KTX cubemap must have six faces.");
        m_width = static_cast<int>(texture->baseWidth);
        m_height = static_cast<int>(texture->baseHeight);
        m_channel = static_cast<int>(std::max(1u, ktxTexture2_GetNumComponents(texture)));

        const ktx_size_t image_size = ktxTexture_GetImageSize(ktxTexture(texture), 0);
        const ktx_uint8_t *raw_ktx_data = ktxTexture_GetData(ktxTexture(texture));
        m_data.resize(static_cast<size_t>(image_size) * 6);

        for (int face = 0; face < 6; ++face) {
            ktx_size_t image_offset = 0;
            const auto offset_error =
                ktxTexture_GetImageOffset(ktxTexture(texture), 0, 0, static_cast<ktx_uint32_t>(face), &image_offset);
            assert(offset_error == KTX_SUCCESS);

            std::memcpy(
                m_data.data() + static_cast<size_t>(face) * static_cast<size_t>(image_size),
                raw_ktx_data + image_offset,
                static_cast<size_t>(image_size)
            );
        }

        TextureAsset::load_asset_from_archive(archive);
    }
} // namespace Engine
