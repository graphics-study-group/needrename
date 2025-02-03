#include "Image2DTextureAsset.h"
#include <stb_image.h>
#include <stb_image_write.h>

namespace Engine
{
    static void write_png_to_mem(void *context, void *data, int size)
    {
        auto &extra_data = *reinterpret_cast<std::vector<std::byte> *>(context);
        extra_data.insert(extra_data.end(), reinterpret_cast<std::byte *>(data), reinterpret_cast<std::byte *>(data) + size);
    }

    void Image2DTextureAsset::save_asset_to_archive(Serialization::Archive &archive) const
    {
        auto &data = archive.m_context->extra_data;
        assert(data.empty());

        stbi_flip_vertically_on_write(false); // this is a static variable, so we need to reset it every time
        stbi_write_png_to_func(write_png_to_mem, &data, m_width, m_height, m_channel, m_data.data(), 0);

        Asset::save_asset_to_archive(archive);
    }

    void Image2DTextureAsset::load_asset_from_archive(Serialization::Archive &archive)
    {
        auto &data = archive.m_context->extra_data;

        stbi_set_flip_vertically_on_load(false); // this is a static variable, so we need to reset it every time
        int width, height, channel;
        stbi_uc *raw_image_data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data.data()), data.size(), &width, &height, &channel, 0);
        assert(raw_image_data);

        m_data.resize(width * height * channel);
        std::memcpy(m_data.data(), raw_image_data, width * height * channel);
        stbi_image_free(raw_image_data);

        Asset::load_asset_from_archive(archive);
        assert(width == m_width && height == m_height && channel == m_channel);
    }

    void Image2DTextureAsset::LoadFromFile(const std::filesystem::path &path)
    {
        m_name = path.stem().string();
        stbi_set_flip_vertically_on_load(true); // this is a static variable, so we need to reset it every time
        stbi_uc *raw_image_data = stbi_load(path.string().c_str(), &m_width, &m_height, &m_channel, 4);
        assert(raw_image_data);
        m_data.resize(m_width * m_height * m_channel);
        std::memcpy(m_data.data(), raw_image_data, m_width * m_height * m_channel);
        stbi_image_free(raw_image_data);
        m_format = ImageUtils::ImageFormat::R8G8B8A8SRGB;
        m_mip_level = 1;
    }

    std::byte *Image2DTextureAsset::GetPixelData()
    {
        return m_data.data();
    }

    size_t Image2DTextureAsset::GetPixelDataSize() const
    {
        return m_data.size();
    }
}
