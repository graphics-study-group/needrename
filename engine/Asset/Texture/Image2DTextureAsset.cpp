#include "Image2DTextureAsset.h"
#include <stb_image.h>

namespace Engine
{
    void Image2DTextureAsset::save_asset_to_archive(Serialization::Archive &archive) const
    {
        __serialization_save__(archive);
        // TODO: Save the image data
    }

    void Image2DTextureAsset::load_asset_from_archive(Serialization::Archive &archive)
    {
        __serialization_load__(archive);
        // TODO: Load the image data
    }

    void Image2DTextureAsset::Unload()
    {
        m_data.clear();
        Asset::SetValid(false);
    }

    void Image2DTextureAsset::LoadFromMemory(const std::byte *data, size_t size)
    {
        stbi_set_flip_vertically_on_load(true);
        stbi_uc *raw_image_data = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data), size, &m_width, &m_height, &m_channel, 4);
        assert(raw_image_data);
        m_data.resize(m_width * m_height * 4);
        std::memcpy(m_data.data(), raw_image_data, m_width * m_height * 4);
        stbi_image_free(raw_image_data);
        m_format = ImageUtils::ImageFormat::R8G8B8A8SRGB;
        m_mip_level = 1;
        Asset::SetValid(true);
    }

    void Image2DTextureAsset::LoadFromFile(const std::filesystem::path &path)
    {
        m_name = path.filename().string();
        stbi_set_flip_vertically_on_load(true);
        stbi_uc *raw_image_data = stbi_load(path.string().c_str(), &m_width, &m_height, &m_channel, 4);
        assert(raw_image_data);
        m_data.resize(m_width * m_height * 4);
        std::memcpy(m_data.data(), raw_image_data, m_width * m_height * 4);
        stbi_image_free(raw_image_data);
        m_format = ImageUtils::ImageFormat::R8G8B8A8SRGB;
        m_mip_level = 1;
        Asset::SetValid(true);
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
