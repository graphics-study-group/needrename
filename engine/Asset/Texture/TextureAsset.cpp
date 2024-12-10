#include "TextureAsset.h"

namespace Engine
{
    void TextureAsset::save_asset_to_archive(Serialization::Archive& archive) const
    {
        Asset::save_asset_to_archive(archive);

        Serialization::Json &json = *archive.m_cursor;
        json["m_name"] = m_name;

        auto &data = archive.m_context->extra_data;
        assert(data.empty());
        data.reserve(m_data.size());

        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_data.data()),
            reinterpret_cast<const std::byte *>(m_data.data() + m_data.size()));
    }

    void TextureAsset::load_asset_from_archive(Serialization::Archive& archive)
    {
        Asset::load_asset_from_archive(archive);

        Serialization::Json &json = *archive.m_cursor;
        m_name = json["m_name"].get<std::string>();

        auto &data = archive.m_context->extra_data;
        m_data.resize(data.size());
        std::memcpy(m_data.data(), data.data(), data.size());
    }
}
