#include "ImageCubemapAsset.h"

#include <Reflection/serialization.h>
#include <array>
#include <assert.h>
#include <cstring>
#include <stb_image.h>

namespace Engine {
    void ImageCubemapAsset::LoadFromFile(const std::array<std::filesystem::path, 6> &paths) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        auto first_image = stbi_load(paths[0].string().c_str(), &width, &height, &channels, 4);
        assert(first_image);

        auto image_size = width * height * 4;
        m_data.resize(image_size * 6);
        std::memcpy(m_data.data(), first_image, image_size);
        stbi_image_free(first_image);

        for (int i = 1; i < 6; i++) {
            int nw, nh, nc;
            stbi_set_flip_vertically_on_load(true);
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
        size_t extra_data_id = archive.create_new_extra_data_buffer(".skybox");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_data.data()),
            reinterpret_cast<const std::byte *>(m_data.data() + m_data.size())
        );

        TextureAsset::save_asset_to_archive(archive);
    }

    void ImageCubemapAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &json = *archive.m_cursor;
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];
        m_data.resize(data.size());
        std::memcpy(
            m_data.data(),
            reinterpret_cast<const std::byte *>(data.data()),
            data.size()
        );

        TextureAsset::load_asset_from_archive(archive);
    }
} // namespace Engine
