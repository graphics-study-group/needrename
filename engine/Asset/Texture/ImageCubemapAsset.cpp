#include "ImageCubemapAsset.h"

#include <Render/ImageUtilsFunc.h>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <ktx.h>
#include <thread>

#include <memory>

#include <Reflection/serialization.h>

namespace {
    void TryCompressTextureToBc7(ktxTexture2 *texture) {
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
            return;
        }

        const auto transcode_error = ktxTexture2_TranscodeBasis(texture, KTX_TTF_BC7_RGBA, KTX_TF_HIGH_QUALITY);
        if (transcode_error != KTX_SUCCESS) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "ktxTexture2_TranscodeBasis failed (%s). Falling back to uncompressed KTX2.",
                ktxErrorString(transcode_error)
            );
        }
    }
} // namespace

namespace Engine {
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

        const vk::Format vk_format = ImageUtils::GetVkFormat(m_format);
        assert(vk_format != vk::Format::eUndefined);

        ktxTextureCreateInfo create_info{};
        create_info.vkFormat = static_cast<ktx_uint32_t>(vk_format);
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

        if (ImageUtils::CanCompressToBc7(m_format)) {
            TryCompressTextureToBc7(texture);
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
        m_format = ImageUtils::FromVkFormat(static_cast<vk::Format>(texture->vkFormat));

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
