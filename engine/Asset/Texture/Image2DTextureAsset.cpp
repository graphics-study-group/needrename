#include "Image2DTextureAsset.h"

#include <Reflection/serialization.h>
#include <Render/ImageUtilsFunc.h>
#include <SDL3/SDL_log.h>
#include <ktx.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

namespace {
    /**
     * @brief Try to compress the texture to Basis Universal format.
     * 
     * If the compression fails, it will log a warning and keep the original uncompressed texture.
     */
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

    /**
     * @brief Create a ktxTexture2 from raw pixel data.
     * 
     * The data should be the image pixel data decoded from an image file, without any header, metadata or compression.
     */
    ktxTexture2 *Create2DTextureFromData(int width, int height, vk::Format format, const std::byte *data, size_t size) {
        ktxTextureCreateInfo create_info{};
        create_info.vkFormat = static_cast<ktx_uint32_t>(format);
        create_info.baseWidth = static_cast<ktx_uint32_t>(width);
        create_info.baseHeight = static_cast<ktx_uint32_t>(height);
        create_info.baseDepth = 1;
        create_info.numDimensions = 2;
        create_info.numLevels = 1;
        create_info.numLayers = 1;
        create_info.numFaces = 1;
        create_info.isArray = KTX_FALSE;
        create_info.generateMipmaps = KTX_FALSE;

        ktxTexture2 *texture = nullptr;
        const auto create_error = ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
        if (create_error != KTX_SUCCESS || texture == nullptr) {
            return nullptr;
        }

        const auto set_image_error = ktxTexture_SetImageFromMemory(
            ktxTexture(texture), 0, 0, 0, reinterpret_cast<const ktx_uint8_t *>(data), static_cast<ktx_size_t>(size)
        );
        if (set_image_error != KTX_SUCCESS) {
            ktxTexture2_Destroy(texture);
            return nullptr;
        }

        return texture;
    }
} // namespace

namespace Engine {
    Image2DTextureAsset::Image2DTextureAsset() {
    }

    Image2DTextureAsset::~Image2DTextureAsset() {
        if (m_texture != nullptr) {
            ktxTexture2_Destroy(m_texture);
        }
    }

    void Image2DTextureAsset::SetDecodedData(
        int width,
        int height,
        int channel,
        std::vector<std::byte> data,
        ImageUtils::ImageFormat format,
        unsigned mip_level
    ) {
        const vk::Format vk_format = ImageUtils::GetVkFormat(format);
        assert(vk_format != vk::Format::eUndefined);

        ktxTexture2 *texture = Create2DTextureFromData(width, height, vk_format, data.data(), data.size());
        assert(texture != nullptr);

        ResetTexture(texture);
        m_width = width;
        m_height = height;
        m_channel = channel;
        m_format = format;
        m_mip_level = mip_level;
    }

    void Image2DTextureAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        assert(m_texture != nullptr);

        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".ktx2");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];

        ktxTexture2 *saved_texture = nullptr;
        auto create_error = ktxTexture2_CreateCopy(m_texture, &saved_texture);
        assert(create_error == KTX_SUCCESS && saved_texture != nullptr);
        std::unique_ptr<ktxTexture2, void (*)(ktxTexture2 *)> texture_guard(saved_texture, ktxTexture2_Destroy);

        if (ImageUtils::CanCompressToBasis(m_format)) {
            TryCompressTextureToBasis(saved_texture);
        }

        ktx_uint8_t *raw_ktx_data = nullptr;
        ktx_size_t raw_ktx_size = 0;
        const auto write_error = ktxTexture_WriteToMemory(ktxTexture(saved_texture), &raw_ktx_data, &raw_ktx_size);
        assert(write_error == KTX_SUCCESS && raw_ktx_data != nullptr);

        data.resize(static_cast<size_t>(raw_ktx_size));
        std::memcpy(data.data(), raw_ktx_data, static_cast<size_t>(raw_ktx_size));
        std::free(raw_ktx_data);

        Asset::save_asset_to_archive(archive);
    }

    void Image2DTextureAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &json = *archive.m_cursor;
        Asset::load_asset_from_archive(archive);
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];
        ktxTexture2 *loaded_texture = nullptr;
        auto create_error = ktxTexture2_CreateFromMemory(
            reinterpret_cast<const ktx_uint8_t *>(data.data()), data.size(), 0, &loaded_texture
        );
        assert(create_error == KTX_SUCCESS && loaded_texture != nullptr);
        ResetTexture(loaded_texture);

        if (ktxTexture2_NeedsTranscoding(m_texture)) {
            const auto transcode_error = ktxTexture2_TranscodeBasis(m_texture, KTX_TTF_RGBA32, 0);
            if (transcode_error != KTX_SUCCESS) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Texture transcode to RGBA32 failed (%s).",
                    ktxErrorString(transcode_error)
                );
            }
            assert(transcode_error == KTX_SUCCESS);
        }
    }

    const std::byte *Image2DTextureAsset::GetPixelData() const {
        return reinterpret_cast<const std::byte *>(ktxTexture_GetData(ktxTexture(m_texture)));
    }

    size_t Image2DTextureAsset::GetPixelDataSize() const {
        if (m_texture == nullptr) {
            return 0;
        }
        return ktxTexture_GetDataSize(ktxTexture(m_texture));
    }

    void Image2DTextureAsset::ResetTexture(ktxTexture2 *texture) {
        if (m_texture != nullptr) {
            ktxTexture2_Destroy(m_texture);
        }
        m_texture = texture;
    }
} // namespace Engine
