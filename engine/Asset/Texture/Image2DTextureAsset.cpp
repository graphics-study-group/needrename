#include "Image2DTextureAsset.h"

#include <Reflection/serialization.h>
#include <Render/ImageUtilsFunc.h>
#include <SDL3/SDL_log.h>
#include <ktx.h>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
    Engine::ImageUtils::ImageFormat FromVkFormat(vk::Format format) {
        switch (format) {
        case vk::Format::eR8G8B8A8Snorm:
            return Engine::ImageUtils::ImageFormat::R8G8B8A8SNorm;
        case vk::Format::eR8G8B8A8Unorm:
            return Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm;
        case vk::Format::eR8G8B8A8Srgb:
            return Engine::ImageUtils::ImageFormat::R8G8B8A8SRGB;
        case vk::Format::eBc7UnormBlock:
            return Engine::ImageUtils::ImageFormat::BC7UNorm;
        case vk::Format::eBc7SrgbBlock:
            return Engine::ImageUtils::ImageFormat::BC7SRGB;
        case vk::Format::eB10G11R11UfloatPack32:
            return Engine::ImageUtils::ImageFormat::R11G11B10UFloat;
        case vk::Format::eR32G32B32A32Sfloat:
            return Engine::ImageUtils::ImageFormat::R32G32B32A32SFloat;
        case vk::Format::eD32Sfloat:
            return Engine::ImageUtils::ImageFormat::D32SFLOAT;
        default:
            return Engine::ImageUtils::ImageFormat::UNDEFINED;
        }
    }
} // namespace

namespace Engine {
    void Image2DTextureAsset::save_asset_to_archive(Serialization::Archive &archive) const {
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
        create_info.numFaces = 1;
        create_info.isArray = KTX_FALSE;
        create_info.generateMipmaps = KTX_FALSE;

        ktxTexture2 *texture = nullptr;
        const auto create_error = ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
        assert(create_error == KTX_SUCCESS && texture != nullptr);
        std::unique_ptr<ktxTexture2, void (*)(ktxTexture2 *)> texture_guard(texture, ktxTexture2_Destroy);

        const auto set_image_error = ktxTexture_SetImageFromMemory(
            ktxTexture(texture),
            0,
            0,
            0,
            reinterpret_cast<const ktx_uint8_t *>(m_data.data()),
            static_cast<ktx_size_t>(m_data.size())
        );
        assert(set_image_error == KTX_SUCCESS);

        ktx_uint8_t *raw_ktx_data = nullptr;
        ktx_size_t raw_ktx_size = 0;
        const auto write_error = ktxTexture_WriteToMemory(ktxTexture(texture), &raw_ktx_data, &raw_ktx_size);
        assert(write_error == KTX_SUCCESS && raw_ktx_data != nullptr);

        data.resize(static_cast<size_t>(raw_ktx_size));
        std::memcpy(data.data(), raw_ktx_data, static_cast<size_t>(raw_ktx_size));
        std::free(raw_ktx_data);

        Asset::save_asset_to_archive(archive);
    }

    void Image2DTextureAsset::load_asset_from_archive(Serialization::Archive &archive) {
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
            auto transcode_error = ktxTexture2_TranscodeBasis(texture, KTX_TTF_BC7_RGBA, KTX_TF_HIGH_QUALITY);
            if (transcode_error != KTX_SUCCESS) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "BC7 transcode failed (%s). Fallback to RGBA32.",
                    ktxErrorString(transcode_error)
                );
                transcode_error = ktxTexture2_TranscodeBasis(texture, KTX_TTF_RGBA32, 0);
                assert(transcode_error == KTX_SUCCESS);
            }
        }

        const int width = static_cast<int>(texture->baseWidth);
        const int height = static_cast<int>(texture->baseHeight);
        const int channel = static_cast<int>(std::max(1u, ktxTexture2_GetNumComponents(texture)));

        ktx_size_t image_offset = 0;
        const auto offset_error = ktxTexture_GetImageOffset(ktxTexture(texture), 0, 0, 0, &image_offset);
        assert(offset_error == KTX_SUCCESS);

        const ktx_size_t image_size = ktxTexture_GetImageSize(ktxTexture(texture), 0);
        const ktx_uint8_t *raw_ktx_data = ktxTexture_GetData(ktxTexture(texture));
        m_data.resize(static_cast<size_t>(image_size));
        std::memcpy(m_data.data(), raw_ktx_data + image_offset, static_cast<size_t>(image_size));

        m_width = width;
        m_height = height;
        m_channel = channel;
        m_mip_level = 1;
        m_format = FromVkFormat(static_cast<vk::Format>(texture->vkFormat));
        if (m_format == ImageUtils::ImageFormat::UNDEFINED) {
            m_format = ImageUtils::ImageFormat::R8G8B8A8UNorm;
        }

        Asset::load_asset_from_archive(archive);
        assert(width == m_width && height == m_height && channel == m_channel);
    }

    const std::byte *Image2DTextureAsset::GetPixelData() const {
        return m_data.data();
    }

    size_t Image2DTextureAsset::GetPixelDataSize() const {
        return m_data.size();
    }
} // namespace Engine
