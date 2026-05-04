#include "ImageCubemapAsset.h"

#include <Render/ImageUtilsFunc.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ktx.h>
#include <thread>

#include <memory>

#include <Reflection/serialization.h>

namespace {
    bool TryCompressTextureToBasis(ktxTexture2 *texture) {
        ktxBasisParams params{};
        params.structSize = sizeof(params);
        params.codec = KTX_BASIS_CODEC_UASTC_LDR_4x4;
        params.uastcFlags = static_cast<ktx_pack_uastc_flags>(KTX_PACK_UASTC_LEVEL_FASTEST);
        params.uastcRDO = KTX_FALSE;
        params.threadCount = std::max(1u, std::thread::hardware_concurrency());

        const auto compress_error = ktxTexture2_CompressBasisEx(texture, &params);
        if (compress_error != KTX_SUCCESS) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "ktxTexture2_CompressBasisEx failed (%s). Falling back to uncompressed KTX2.",
                ktxErrorString(compress_error)
            );
            return false;
        }

        return true;
    }

    ktxTexture2 *CreateCubemapTextureFromMemory(
        int width, int height, vk::Format format, const std::byte *data, size_t size
    ) {
        assert(size % 6 == 0);

        ktxTextureCreateInfo create_info{};
        create_info.vkFormat = static_cast<ktx_uint32_t>(format);
        create_info.baseWidth = static_cast<ktx_uint32_t>(width);
        create_info.baseHeight = static_cast<ktx_uint32_t>(height);
        create_info.baseDepth = 1;
        create_info.numDimensions = 2;
        create_info.numLevels = 1;
        create_info.numLayers = 1;
        create_info.numFaces = 6;
        create_info.isArray = KTX_FALSE;
        create_info.generateMipmaps = KTX_FALSE;

        ktxTexture2 *texture = nullptr;
        const auto create_error = ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
        if (create_error != KTX_SUCCESS || texture == nullptr) {
            return nullptr;
        }

        const size_t face_size = size / 6;
        for (int face = 0; face < 6; ++face) {
            const auto set_image_error = ktxTexture_SetImageFromMemory(
                ktxTexture(texture),
                0,
                0,
                static_cast<ktx_uint32_t>(face),
                reinterpret_cast<const ktx_uint8_t *>(data + face * face_size),
                static_cast<ktx_size_t>(face_size)
            );
            if (set_image_error != KTX_SUCCESS) {
                ktxTexture2_Destroy(texture);
                return nullptr;
            }
        }

        return texture;
    }
} // namespace

namespace Engine {
    ImageCubemapAsset::ImageCubemapAsset() {
    }

    ImageCubemapAsset::~ImageCubemapAsset() {
        if (m_texture != nullptr) {
            ktxTexture2_Destroy(m_texture);
        }
    }

    void ImageCubemapAsset::SetDecodedData(
        int width, int height, int channel, std::vector<std::byte> data, ImageUtils::ImageFormat format
    ) {
        const vk::Format vk_format = ImageUtils::GetVkFormat(format);
        assert(vk_format != vk::Format::eUndefined);

        ktxTexture2 *texture = CreateCubemapTextureFromMemory(width, height, vk_format, data.data(), data.size());
        assert(texture != nullptr);

        ResetTexture(texture);
        m_width = width;
        m_height = height;
        m_channel = channel;
        m_format = format;
    }

    const std::byte *ImageCubemapAsset::GetPixelData() const {
        if (m_texture == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<const std::byte *>(ktxTexture_GetData(ktxTexture(m_texture)));
    }

    size_t ImageCubemapAsset::GetPixelDataSize() const {
        if (m_texture == nullptr) {
            return 0;
        }
        return ktxTexture_GetDataSize(ktxTexture(m_texture));
    }

    void ImageCubemapAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        assert(m_texture != nullptr);

        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".ktx2");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];

        auto saved_texture = CreateCubemapTextureFromMemory(
            m_width, m_height, ImageUtils::GetVkFormat(m_format), GetPixelData(), GetPixelDataSize()
        );
        std::unique_ptr<ktxTexture2, void (*)(ktxTexture2 *)> texture_guard(saved_texture, ktxTexture2_Destroy);

        if (ImageUtils::CanCompressToBc7(m_format)) {
            TryCompressTextureToBasis(saved_texture);
        }

        ktx_uint8_t *raw_ktx_data = nullptr;
        ktx_size_t raw_ktx_size = 0;
        const auto write_error = ktxTexture_WriteToMemory(ktxTexture(saved_texture), &raw_ktx_data, &raw_ktx_size);
        assert(write_error == KTX_SUCCESS && raw_ktx_data != nullptr);

        data.resize(static_cast<size_t>(raw_ktx_size));
        std::memcpy(data.data(), raw_ktx_data, static_cast<size_t>(raw_ktx_size));
        std::free(raw_ktx_data);

        TextureAsset::save_asset_to_archive(archive);
    }

    void ImageCubemapAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &json = *archive.m_cursor;
        TextureAsset::load_asset_from_archive(archive);
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];
        SetDecodedData(m_width, m_height, m_channel, std::vector<std::byte>(data.begin(), data.end()), m_format);

        if (ktxTexture2_NeedsTranscoding(m_texture)) {
            const auto transcode_error = ktxTexture2_TranscodeBasis(m_texture, KTX_TTF_RGBA32, 0);
            if (transcode_error != KTX_SUCCESS) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Cubemap transcode to RGBA32 failed (%s).",
                    ktxErrorString(transcode_error)
                );
            }
            assert(transcode_error == KTX_SUCCESS);
        }
        assert(m_texture->numFaces == 6 && "KTX cubemap must have six faces.");
    }

    void ImageCubemapAsset::ResetTexture(ktxTexture2 *texture) {
        if (m_texture != nullptr) {
            ktxTexture2_Destroy(m_texture);
        }
        m_texture = texture;
    }
} // namespace Engine
